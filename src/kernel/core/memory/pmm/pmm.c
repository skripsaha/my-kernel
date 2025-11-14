#include "pmm.h"
#include "e820.h"
#include "klib.h"

typedef struct {
    uintptr_t base;
    size_t pages;
    uint8_t* bitmap;
    spinlock_t lock;
    size_t last_free;
} pmm_zone_t;

static pmm_zone_t pmm_zone;
static bool pmm_initialized = false;

// Внутренние функции
static void pmm_reserve_region(uintptr_t base, uintptr_t end, const char* name);
static void pmm_set_bit(size_t bit, pmm_frame_state_t state);
static pmm_frame_state_t pmm_get_bit(size_t bit);
static size_t pmm_find_free_sequence(size_t count);


void pmm_init(void) {
    if (pmm_initialized) {
        kprintf("[PMM] Already initialized!\n");
        return;
    }

    kprintf("[PMM] Fetching e820 memory map from bootloader...\n");
    e820_entry_t* entries = memory_map_get_entries();
    size_t entry_count = memory_map_get_entry_count();

    // Sanity check
    if (entry_count == 0 || entries == NULL) {
        panic("[PMM] CRITICAL: E820 map is empty! Bootloader failed to detect memory!");
    }

    if (entry_count > 100) {
        kprintf("[PMM] WARNING: Suspiciously high entry count (%u), possible corruption\n", (unsigned int)entry_count);
    }

    kprintf("[PMM] Total e820 entries: %u\n", (unsigned int)entry_count);

    // Print all entries for debug
    for (size_t i = 0; i < entry_count && i < 20; i++) {  // Limit to 20 to avoid spam
        // Split 64-bit values into 32-bit parts for printing
        uint32_t base_low = (uint32_t)(entries[i].base & 0xFFFFFFFF);
        uint32_t base_high = (uint32_t)(entries[i].base >> 32);
        uint32_t len_low = (uint32_t)(entries[i].length & 0xFFFFFFFF);
        uint32_t len_high = (uint32_t)(entries[i].length >> 32);

        kprintf("  E820[%u]: base=0x%x%08x len=0x%x%08x type=%u\n",
                (unsigned int)i, base_high, base_low, len_high, len_low, (unsigned int)entries[i].type);
    }

    // Find the highest usable RAM address
    uintptr_t mem_end = 0;
    kprintf("[PMM] Scanning for USABLE RAM (type=%u)...\n", (unsigned int)E820_USABLE);

    for (size_t i = 0; i < entry_count; i++) {
        if (entries[i].type == E820_USABLE && entries[i].length > 0) {
            uintptr_t region_end = entries[i].base + entries[i].length;
            kprintf("[PMM] Entry %u: USABLE from %p to %p\n",
                    (unsigned int)i, (void*)entries[i].base, (void*)region_end);
            if (region_end > mem_end) {
                mem_end = region_end;
            }
        }
    }

    kprintf("[PMM] Highest usable address (mem_end) = %p\n", (void*)mem_end);

    if (mem_end <= 0x100000) {
        panic("[PMM] ERROR: Not enough usable RAM!");
    }

    // PMM will manage memory starting from 1MB (0x100000)
    pmm_zone.base = 0x100000;

    // BUGFIX: Prevent underflow if mem_end < pmm_zone.base
    if (mem_end <= pmm_zone.base) {
        kprintf("[PMM] ERROR: mem_end (0x%p) <= pmm_zone.base (0x%p)\n",
                (void*)mem_end, (void*)pmm_zone.base);
        panic("[PMM] ERROR: Not enough usable RAM above 1MB!");
    }

    pmm_zone.pages = (mem_end - pmm_zone.base) / PMM_PAGE_SIZE;
    kprintf("[PMM] Managing pages from 0x%p, total pages = %zu\n",
            (void*)pmm_zone.base, pmm_zone.pages);

    if (pmm_zone.pages == 0) {
        panic("[PMM] ERROR: No usable pages!");
    }

    // Bitmap size (1 bit per page)
    size_t bitmap_size = (pmm_zone.pages + 7) / 8;
    kprintf("[PMM] Bitmap size = %zu bytes (%zu KB)\n",
            bitmap_size, bitmap_size / 1024);

    // BUGFIX: Sanity check on bitmap size (prevent huge allocations)
    size_t max_bitmap_size = 16 * 1024 * 1024; // 16MB max (handles up to 512GB RAM)
    if (bitmap_size > max_bitmap_size) {
        kprintf("[PMM] WARNING: Bitmap too large (%zu bytes), capping at %zu bytes\n",
                bitmap_size, max_bitmap_size);
        bitmap_size = max_bitmap_size;
        pmm_zone.pages = bitmap_size * 8; // Recalculate pages
    }

    // Place bitmap at the end of RAM
    // pmm_zone.bitmap = (uint8_t*)(mem_end - bitmap_size);
    // kprintf("[PMM] Bitmap placed at %p\n", pmm_zone.bitmap);
    pmm_zone.bitmap = (uint8_t*)ALIGN_UP((uintptr_t)&_kernel_end, 4096);
    kprintf("[PMM] Bitmap placed at %p (after kernel, %zu KB)\n", pmm_zone.bitmap, bitmap_size / 1024);

    // Ensure the bitmap is within managed range
    if ((uintptr_t)pmm_zone.bitmap < pmm_zone.base) {
        panic("[PMM] ERROR: Bitmap is outside managed memory!");
    }

    // BUGFIX: Add sanity check before memset to prevent hang
    if (bitmap_size > 100 * 1024 * 1024) {  // 100MB sanity limit
        kprintf("[PMM] ERROR: Refusing to memset %zu bytes (too large!)\n", bitmap_size);
        panic("[PMM] ERROR: Bitmap size unreasonable!");
    }

    // Mark all pages as used (safe default)
    memset(pmm_zone.bitmap, 0xFF, bitmap_size);
    kprintf("[PMM] Bitmap initialized, marking usable regions...\n");
    for (size_t i = 0; i < entry_count; i++) {
        if (entries[i].type == E820_USABLE && entries[i].length > 0) {
            uintptr_t start = entries[i].base;
            uintptr_t end = entries[i].base + entries[i].length;
            if (end <= pmm_zone.base) continue;
            if (start < pmm_zone.base) start = pmm_zone.base;
            if (start >= end) continue;

            size_t start_page = (start - pmm_zone.base) / PMM_PAGE_SIZE;
            size_t end_page = (end - pmm_zone.base) / PMM_PAGE_SIZE;
            if (end_page > pmm_zone.pages) end_page = pmm_zone.pages;

            size_t pages_to_free = end_page - start_page;
            kprintf("[PMM] Freeing %zu pages: %zu .. %zu (0x%p - 0x%p)\n",
                    pages_to_free, start_page, end_page - 1, (void*)start, (void*)end);

            // BUGFIX: Prevent hang on huge page ranges
            if (pages_to_free > 10000000) {  // 10 million pages = 40GB
                kprintf("[PMM] WARNING: Huge page range (%zu pages), skipping!\n", pages_to_free);
                continue;
            }

            // Show progress for large ranges
            for (size_t j = start_page; j < end_page; j++) {
                pmm_set_bit(j, PMM_FRAME_FREE);
                // Progress indicator for large ranges (every 100k pages)
                if (pages_to_free > 100000 && (j - start_page) % 100000 == 0) {
                    kprintf("[PMM] Progress: %zu / %zu pages freed\n",
                            j - start_page, pages_to_free);
                }
            }
            kprintf("[PMM] Freed %zu pages successfully\n", pages_to_free);
        }
    }
    kprintf("[PMM] DEBUG: Finished freeing e820 regions\n");

    // Optionally, reserve kernel and bitmap here, if you have _kernel_start/_kernel_end
    extern uintptr_t _kernel_start;
    extern uintptr_t _kernel_end;

    kprintf("[PMM] DEBUG: Reserving kernel region 0x%p - 0x%p\n",
            (void*)&_kernel_start, (void*)&_kernel_end);
    pmm_reserve_region((uintptr_t)&_kernel_start, (uintptr_t)&_kernel_end, "Kernel");

    kprintf("[PMM] DEBUG: Reserving bitmap region 0x%p - 0x%p (%zu bytes)\n",
            (void*)pmm_zone.bitmap, (void*)(pmm_zone.bitmap + bitmap_size), bitmap_size);
    pmm_reserve_region((uintptr_t)pmm_zone.bitmap, (uintptr_t)pmm_zone.bitmap + bitmap_size, "Bitmap");

    kprintf("[PMM] DEBUG: Initializing spinlock\n");
    spinlock_init(&pmm_zone.lock);

    kprintf("[PMM] DEBUG: Setting pmm_initialized = true\n");
    pmm_initialized = true;

    kprintf("[PMM] Initialized: %zu MB available, %zu pages.\n",
        (pmm_zone.pages * PMM_PAGE_SIZE) / (1024 * 1024),
        pmm_zone.pages
    );
    kprintf("[PMM] DEBUG: pmm_init() completed successfully!\n");
}


// void pmm_init(void) {
//     if (pmm_initialized) return;

//     // Получаем память из E820
//     e820_entry_t* entries = memory_map_get_entries();
//     size_t entry_count = memory_map_get_entry_count();

//     // Находим максимальный доступный регион
//     uintptr_t mem_end = 0;
//     for (size_t i = 0; i < entry_count; i++) {
//         if (entries[i].type == E820_USABLE && 
//             entries[i].base + entries[i].length > mem_end) {
//             mem_end = entries[i].base + entries[i].length;
//         }
//     }

//     // Инициализируем зону памяти (начинаем с 1MB)
//     pmm_zone.base = 0x100000;
//     pmm_zone.pages = (mem_end - pmm_zone.base) / PMM_PAGE_SIZE;
    
//     // Размещаем битовую карту в конце памяти
//     size_t bitmap_size = (pmm_zone.pages + PMM_BITMAP_ALIGN - 1) / PMM_BITMAP_ALIGN;
//     pmm_zone.bitmap = (uint8_t*)(mem_end - bitmap_size);
    
//     // Помечаем всю память как занятую
//     memset(pmm_zone.bitmap, 0xFF, bitmap_size);
    
//     // Освобождаем доступные регионы
//     for (size_t i = 0; i < entry_count; i++) {
//         if (entries[i].type == E820_USABLE) {
//             uintptr_t start = MAX(entries[i].base, pmm_zone.base);
//             uintptr_t end = MIN(entries[i].base + entries[i].length, mem_end);
            
//             if (start < end) {
//                 size_t start_page = (start - pmm_zone.base) / PMM_PAGE_SIZE;
//                 size_t end_page = (end - pmm_zone.base) / PMM_PAGE_SIZE;
                
//                 for (size_t j = start_page; j < end_page; j++) {
//                     pmm_set_bit(j, PMM_FRAME_FREE);
//                 }
//             }
//         }
//     }
    
//     // Резервируем системные области
//     extern uintptr_t _kernel_start;
//     extern uintptr_t _kernel_end;
//     pmm_reserve_region((uintptr_t)&_kernel_start, (uintptr_t)&_kernel_end, "Kernel");
//     pmm_reserve_region((uintptr_t)pmm_zone.bitmap, (uintptr_t)pmm_zone.bitmap + bitmap_size, "Bitmap");
    
//     spinlock_init(&pmm_zone.lock);
//     pmm_initialized = true;
    
//     kprintf("PMM: Initialized with %d MB available\n", 
//            (pmm_zone.pages * PMM_PAGE_SIZE) / (1024 * 1024));
// }

void* pmm_alloc(size_t pages) {
    if (!pages || !pmm_initialized) return NULL;
    
    spin_lock(&pmm_zone.lock);
    
    size_t start = pmm_find_free_sequence(pages);
    if (start == (size_t)-1) {
        spin_unlock(&pmm_zone.lock);
        return NULL;
    }
    
    for (size_t i = 0; i < pages; i++) {
        pmm_set_bit(start + i, PMM_FRAME_USED);
    }
    
    void* addr = (void*)(pmm_zone.base + start * PMM_PAGE_SIZE);
    spin_unlock(&pmm_zone.lock);
    return addr;
}

void* pmm_alloc_zero(size_t pages) {
    void* addr = pmm_alloc(pages);
    if (addr) memset(addr, 0, pages * PMM_PAGE_SIZE);
    return addr;
}

void pmm_free(void* addr, size_t pages) {
    if (!addr || !pages || !pmm_initialized) return;

    uintptr_t base = (uintptr_t)addr;
    if (base < pmm_zone.base || base >= pmm_zone.base + pmm_zone.pages * PMM_PAGE_SIZE) {
        // PRODUCTION FIX: Warn instead of panic for invalid free
        kprintf("[PMM] ERROR: Invalid free address %p (outside managed range 0x%p-0x%p)\n",
                addr, (void*)pmm_zone.base,
                (void*)(pmm_zone.base + pmm_zone.pages * PMM_PAGE_SIZE));
        return;  // Return safely instead of panic
    }

    size_t first = (base - pmm_zone.base) / PMM_PAGE_SIZE;

    spin_lock(&pmm_zone.lock);

    // Проверка на двойное освобождение (PRODUCTION FIX: warn instead of panic)
    for (size_t i = first; i < first + pages; i++) {
        if (pmm_get_bit(i) == PMM_FRAME_FREE) {
            spin_unlock(&pmm_zone.lock);
            kprintf("[PMM] ERROR: Double free detected at page %zu (address %p)\n",
                    i, addr);
            kprintf("[PMM] This is a serious bug - please check the caller!\n");
            return;  // Return safely to prevent corruption
        }
    }
    
    // Освобождение
    for (size_t i = first; i < first + pages; i++) {
        pmm_set_bit(i, PMM_FRAME_FREE);
    }
    
    // Обновляем last_free для оптимизации
    if (first < pmm_zone.last_free) {
        pmm_zone.last_free = first;
    }
    
    spin_unlock(&pmm_zone.lock);
}

// Внутренние функции
static void pmm_reserve_region(uintptr_t base, uintptr_t end, const char* name) {
    base = ALIGN_DOWN(base, PMM_PAGE_SIZE);
    end = ALIGN_UP(end, PMM_PAGE_SIZE);

    if (base >= end) return;

    // BUGFIX: Check if region is below PMM management zone to prevent underflow
    if (end <= pmm_zone.base) {
        kprintf("[PMM] Skipping %s reservation (0x%p-0x%p): below PMM zone (0x%p)\n",
                name, (void*)base, (void*)end, (void*)pmm_zone.base);
        return;
    }

    // Adjust base if it starts below PMM zone
    if (base < pmm_zone.base) {
        kprintf("[PMM] Adjusting %s base from 0x%p to 0x%p (PMM zone start)\n",
                name, (void*)base, (void*)pmm_zone.base);
        base = pmm_zone.base;
    }

    // Now safe to calculate page numbers (no underflow possible)
    size_t start_page = (base - pmm_zone.base) / PMM_PAGE_SIZE;
    size_t end_page = (end - pmm_zone.base) / PMM_PAGE_SIZE;

    // Важно: ограничиваем диапазон резервирования только управляемой PMM зоной
    if (start_page >= pmm_zone.pages) return; // Регион начинается за пределами зоны PMM
    if (end_page > pmm_zone.pages) end_page = pmm_zone.pages; // Регион выходит за пределы зоны PMM

    kprintf("[PMM] Reserving %s: pages %zu-%zu (0x%p-0x%p)\n",
            name, start_page, end_page - 1, (void*)base, (void*)end);

    for (size_t i = start_page; i < end_page; i++) {
        pmm_set_bit(i, PMM_FRAME_RESERVED); // Пометить как занятое (1)
    }

    kprintf("[PMM] Reserved %s at %p-%p (%zu pages)\n",
            name, (void*)base, (void*)end, end_page - start_page);
}

static void pmm_set_bit(size_t bit, pmm_frame_state_t state) {
    size_t byte = bit / PMM_BITMAP_ALIGN;
    size_t offset = bit % PMM_BITMAP_ALIGN; // offset = bit % 8

    switch(state) {
        case PMM_FRAME_FREE:
            pmm_zone.bitmap[byte] &= ~(1 << offset); // Сбросить бит в 0
            break;
        default: // PMM_FRAME_USED, PMM_FRAME_RESERVED, PMM_FRAME_KERNEL, PMM_FRAME_BAD
            pmm_zone.bitmap[byte] |= (1 << offset); // Установить бит в 1
            break;
    }
}

static pmm_frame_state_t pmm_get_bit(size_t bit) {
    size_t byte = bit / PMM_BITMAP_ALIGN;
    size_t offset = bit % PMM_BITMAP_ALIGN; // offset = bit % 8
    // Возвращает PMM_FRAME_USED (1) или PMM_FRAME_FREE (0)
    // Другие состояния не различаются
    return (pmm_zone.bitmap[byte] & (1 << offset)) ? PMM_FRAME_USED : PMM_FRAME_FREE;
}

static size_t pmm_find_free_sequence(size_t count) {
    size_t consecutive = 0;
    size_t start_search_from = pmm_zone.last_free; // Изменил имя переменной для ясности

    // Ищем от last_free до конца
    for (size_t i = start_search_from; i < pmm_zone.pages; i++) {
        if (pmm_get_bit(i) == PMM_FRAME_FREE) {
            consecutive++;
            if (consecutive == count) {
                pmm_zone.last_free = i - count + 1; // Обновляем last_free на место найденного блока
                return i - count + 1;
            }
        } else {
            consecutive = 0;
        }
    }

    // Если не нашли, сбрасываем consecutive и ищем с начала до last_free
    consecutive = 0; // Сброс для нового прохода
    for (size_t i = 0; i < start_search_from; i++) { // Ищем только до last_free
        if (pmm_get_bit(i) == PMM_FRAME_FREE) {
            consecutive++;
            if (consecutive == count) {
                pmm_zone.last_free = i - count + 1; // Обновляем last_free на место найденного блока
                return i - count + 1;
            }
        } else {
            consecutive = 0;
        }
    }

    return (size_t)-1;
}

// Утилиты
size_t pmm_total_pages(void) {
    return pmm_zone.pages;
}

size_t pmm_free_pages(void) {
    size_t count = 0;
    spin_lock(&pmm_zone.lock);
    for (size_t i = 0; i < pmm_zone.pages; i++) {
        if (pmm_get_bit(i) == PMM_FRAME_FREE) count++;
    }
    spin_unlock(&pmm_zone.lock);
    return count;
}

size_t pmm_used_pages(void) {
    return pmm_total_pages() - pmm_free_pages();
}

void pmm_dump_stats(void) {
    kprintf("Physical Memory Manager Statistics:\n");
    kprintf("  Total pages: %d (%d MB)\n", 
           pmm_total_pages(), 
           (pmm_total_pages() * PMM_PAGE_SIZE) / (1024 * 1024));
    kprintf("  Used pages:  %d (%d MB)\n",
           pmm_used_pages(),
           (pmm_used_pages() * PMM_PAGE_SIZE) / (1024 * 1024));
    kprintf("  Free pages:  %d (%d MB)\n",
           pmm_free_pages(),
           (pmm_free_pages() * PMM_PAGE_SIZE) / (1024 * 1024));
}

// Отладочные функции
void pmm_print_memory_map(void) {
    e820_entry_t* entries = memory_map_get_entries();
    size_t entry_count = memory_map_get_entry_count();
    
    kprintf("Memory Map:\n");
    for (size_t i = 0; i < entry_count; i++) {
        kprintf("  %p-%p: %s\n", 
               (void*)entries[i].base,
               (void*)(entries[i].base + entries[i].length),
               entries[i].type == E820_USABLE ? "Usable" : "Reserved");
    }
}

bool pmm_check_integrity(void) {
    // Проверяем, что битовая карта не повреждена
    for (size_t i = 0; i < pmm_zone.pages; i++) {
        uintptr_t addr = pmm_zone.base + i * PMM_PAGE_SIZE;
        
        // Проверяем, что страницы ядра помечены как занятые
        if (addr < (uintptr_t)&_kernel_end && 
            pmm_get_bit(i) != PMM_FRAME_USED) {
            return false;
        }
    }
    return true;
}