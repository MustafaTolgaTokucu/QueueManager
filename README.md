
QueueManager – Queue FIFO System

This project implements a dynamic, multi-queue FIFO byte stream manager within a fixed-size 2048-byte memory buffer (`unsigned char data[2048]`).

Global Memory Layout (2048 Bytes)
---------------------------------

Queue Descriptors (512 B)      // 64 queues × 8 bytes each

Global Metadata (8 B)          // Free list head, unused segment counter, init flag

Segment Pool (~1520 B)       | // 95 segments × 16 bytes each


Queue Descriptor
----------------
Each queue is represented by a `Q` struct (8 bytes):

 struct Q {
    uint16_t head_segment;   // Index of first segment
    uint16_t tail_segment;   // Index of last segment
    uint8_t  head_offset;    // Read position in head segment
    uint8_t  tail_offset;    // Write position in tail segment
    uint8_t  in_use;         // Allocation flag
    uint8_t  pad;            // Padding (for 8-byte alignment)
 };

- Max Queues: 64

Segment-Based Data Storage
---------------------------
Data is stored in fixed-size segments, each 16 bytes:
- 14 bytes of actual payload
- 2 bytes for the `next` pointer (linked list behavior)

struct Segment {
    uint16_t next;             // Next segment index (or INVALID_INDEX)
    unsigned char data[14];    // Actual byte storage
};

Segment Count: 95 segments, allowing ~1.3 KB of FIFO storage across all queues.

Global State
------------
Global allocator state is embedded directly into the buffer:
struct GlobalData {
    uint16_t free_list_head;   // Linked list of free segments
    uint16_t next_unused;      // Next untouched segment index
    uint32_t initialized;      // Set to 1 after first allocation
};

This ensures:
- Free list recycling for released segments

Key Operations
--------------
Create a Queue
- Searches descriptor array for a free slot
- Initializes head/tail to INVALID_INDEX
- Returns a handle to the caller

Enqueue
- Writes byte to the current tail segment
- Allocates a new segment if needed (when tail is full)
- Automatically manages segment chaining

Dequeue
- Reads byte from head segment
- If the segment is full, advances to the next and frees the old one
- If queue becomes empty, frees all segments and resets pointers

Destroy Queue
- Frees all segments in the queue
- Marks descriptor as unused

Error Handling
--------------
- on_out_of_memory(): No memory remains 
- on_illegal_operation(): Invalid operations 

Example
-------
Q* q0 = create_queue();
enqueue_byte(q0, 42);
printf("%d\n", dequeue_byte(q0)); // Output: 42
destroy_queue(q0);

File
----
- QueueManager.cpp – Single-file implementation of the entire system
