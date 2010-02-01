#ifndef LIBTORQUE_HARDWARE_MEMORY
#define LIBTORQUE_HARDWARE_MEMORY

#ifdef __cplusplus
extern "C" {
#endif

struct torque_ctx;
struct libtorque_nodet;

unsigned libtorque_mem_nodecount(const struct torque_ctx *)
	__attribute__ ((visibility("default")))
	__attribute__ ((nonnull(1)));

const struct libtorque_nodet *
libtorque_node_getdesc(const struct torque_ctx *,unsigned)
	__attribute__ ((visibility("default")))
	__attribute__ ((nonnull(1)));

// Remaining declarations are internal to libtorque via -fvisibility=hidden
int detect_memories(struct torque_ctx *)
	__attribute__ ((nonnull(1)));

void free_memories(struct torque_ctx *);

size_t large_system_pagesize(const struct torque_ctx *)
	__attribute__ ((warn_unused_result))
	__attribute__ ((nonnull(1)));

#ifdef __cplusplus
}
#endif

#endif
