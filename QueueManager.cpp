#include <cstdint>
#include <cstdio>
#include <cstdlib> 

// The single permitted global storage block 
unsigned char data[2048] = {0};

// Constants
#define DATA_SIZE 2048                                   // Total bytes available 
#define MAX_QUEUES 64                                    // Queue limit 
#define DESCRIPTOR_SIZE 8                                // Bytes per QueueDescriptor 
#define DESCRIPTORS_AREA (MAX_QUEUES * DESCRIPTOR_SIZE)  // 512 bytes 
#define GLOBAL_DATA_SIZE 8                               // Global data size

// Each segment stores 14 data bytes + 2bytes next index = 16 bytes 
#define SEG_PAYLOAD_SIZE 14                                                          // Bytes per segment 
#define SEG_SIZE (SEG_PAYLOAD_SIZE + 2)                                              // 14+2 = 16 bytes per segment
#define SEGMENT_COUNT ((DATA_SIZE - DESCRIPTORS_AREA - GLOBAL_DATA_SIZE) / SEG_SIZE) // 95 segments 

// No segment 
#define INVALID_INDEX UINT16_MAX

// Structured data types
struct GlobalData {
    uint16_t free_list_head;    // Head of global free list of segments  
    uint16_t next_unused;       // First unused segment index
    uint32_t initialized;       // Initialization flag
};

struct Q {
    uint16_t head_segment;      // Index of first payload segment or INVALID_INDEX
    uint16_t tail_segment;      // Index of last payload segment or INVALID_INDEX
    uint8_t head_offset;        // Current read position inside head_segment 
    uint8_t tail_offset;        // Next free write position inside tail_segment
    uint8_t in_use;             // 0 = free, 1 = allocated (descriptor 0 reserved for globals)
    uint8_t pad;                // Padding so sizeof == 8 (for easy calc & compiler benefit)
};

struct Segment {
    uint16_t next;              // Index of next segment or INVALID_INDEX
    unsigned char data[SEG_PAYLOAD_SIZE];
};

union DataOverlay {
    unsigned char bytes[DATA_SIZE];
    struct {
        Q descriptors[MAX_QUEUES];
        GlobalData global_data;
        Segment segments[SEGMENT_COUNT];
    } layout;
};

DataOverlay& overlay() {
    return *static_cast<DataOverlay*>(static_cast<void*>(data));
}

Q* descriptors() {
    return overlay().layout.descriptors;
}

Segment* segments() {
    return overlay().layout.segments;
}

Segment& seg(uint16_t idx) {
    return segments()[idx];
}

// Global state access
GlobalData& global_data() {
    return overlay().layout.global_data;
}

void on_out_of_memory()
{
	printf("QueueManager: out of memory\n");
    //"will not return"
    std::exit(1); 
}

void on_illegal_operation()
{
	printf("QueueManager: illegal operation\n");
    std::exit(1);
}

// Segment allocator
uint16_t allocate_segment() {
    // Initialize on first use 
    if (!global_data().initialized) {
        global_data().initialized = 1;               
        global_data().free_list_head = INVALID_INDEX; 
        global_data().next_unused = 0;
    }

    // Try the free list first
    if (global_data().free_list_head != INVALID_INDEX) {
        uint16_t idx = global_data().free_list_head;
        global_data().free_list_head = seg(idx).next;
        return idx;
    }

    // Allocate unused segment if any remain
    if (global_data().next_unused < SEGMENT_COUNT) {
        return global_data().next_unused++;
    }

    on_out_of_memory();
    return INVALID_INDEX; // not reached
}

void free_segment(uint16_t idx) {
    seg(idx).next = global_data().free_list_head;
    global_data().free_list_head = idx;
}

// Public interface expected by the assignment
Q *create_queue() {
    Q* d = descriptors();
    // Search for a free descriptor 
    for (std::size_t i = 0; i < MAX_QUEUES; ++i) {
        if (!d[i].in_use) {
            d[i].in_use        = 1;
            d[i].head_segment  = INVALID_INDEX;
            d[i].tail_segment  = INVALID_INDEX;
            d[i].head_offset   = 0;
            d[i].tail_offset   = 0;
            return &d[i];
        }
    }
    on_out_of_memory();
    return nullptr; // not reached
}

void destroy_queue(Q *q) {
    if (!q) on_illegal_operation();

    // Is pointer valid in description range?
    Q* base = descriptors();
    if (!(q >= base && q < base + MAX_QUEUES) || !q->in_use) {
        on_illegal_operation();
    }

    // Return all segments to the global free list
    uint16_t seg_idx = q->head_segment;
    while (seg_idx != INVALID_INDEX) {
        uint16_t next = seg(seg_idx).next;
        free_segment(seg_idx);
        seg_idx = next;
    }
    // Mark descriptor free
    q->in_use = 0;
}

void enqueue_byte(Q *q, unsigned char b) {
    if (!q || !q->in_use) on_illegal_operation();

    // Allocate first segment if queue is empty
    if (q->tail_segment == INVALID_INDEX) {
        uint16_t new_seg = allocate_segment();
        seg(new_seg).next = INVALID_INDEX;
        q->head_segment = q->tail_segment = new_seg;
        q->head_offset = 0;
        q->tail_offset = 0;
    }

    Segment& tail = seg(q->tail_segment);
    tail.data[q->tail_offset++] = b;

    // If the tail segment is full, allocate new 
    if (q->tail_offset == SEG_PAYLOAD_SIZE) {
        uint16_t new_seg = allocate_segment();
        seg(new_seg).next = INVALID_INDEX;
        tail.next = new_seg;
        q->tail_segment = new_seg;
        q->tail_offset  = 0;
    }
}

unsigned char dequeue_byte(Q *q) {
    if (!q || !q->in_use) 
        on_illegal_operation();
    if (q->head_segment == INVALID_INDEX) 
        on_illegal_operation();

    Segment& head = seg(q->head_segment);
    unsigned char value = head.data[q->head_offset++];

    // If last byte of this segment is used
    if (q->head_segment == q->tail_segment && q->head_offset == q->tail_offset) {
        // Queue becomes empty
        free_segment(q->head_segment);
        q->head_segment = q->tail_segment = INVALID_INDEX;
        q->head_offset = q->tail_offset = 0;
        return value;
    }

    // Head segment fully consumed → move to next and free current
    if (q->head_offset == SEG_PAYLOAD_SIZE) {
        uint16_t old_seg = q->head_segment;
        q->head_segment = head.next;
        q->head_offset = 0;
        free_segment(old_seg);
    }

    return value;
}

// TEST CODE
int main() {
    Q* q0 = create_queue();
    enqueue_byte(q0, 0);
    enqueue_byte(q0, 1);
    Q* q1 = create_queue();
    enqueue_byte(q1, 3);
    enqueue_byte(q0, 2);
    enqueue_byte(q1, 4);
    printf("%d", dequeue_byte(q0));
    printf("%d\n", dequeue_byte(q0));
    enqueue_byte(q0, 5);
    enqueue_byte(q1, 6);
    printf("%d", dequeue_byte(q0));
    printf("%d\n", dequeue_byte(q0));
    destroy_queue(q0);
    printf("%d", dequeue_byte(q1));
    printf("%d", dequeue_byte(q1));
    printf("%d\n", dequeue_byte(q1));
    destroy_queue(q1);
    return 0;
}
