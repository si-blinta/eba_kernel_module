#include <linux/types.h>
/**
 * eba_utils_file_to_buf - Reads an entire file into a pre allocated EBA buffer.
 * @param filepath: Path to the file on disk (e.g. "/var/lib/eba/somefile.eba").
 * @param buff_id: Id of the buffer.
 *
 * @return: 0 on success and  1 on error.
 */
int eba_utils_file_to_buf(const char *filepath, uint64_t buff_id);


/**
 * eba_utils_buf_to_file - Writes the entire content of an EBA buffer to a file.
 * @param buff_id: The EBA buffer id.
 * @param size: The size of the data in that buffer.
 * @param filepath: Path to the file on disk (e.g. "/var/lib/eba/dump.eba").
 *
 * @return: 0 on success, negative error code on failure.
 */
int eba_utils_buf_to_file(uint64_t buff_id, uint64_t size, const char *filepath);


/**
 * eba_export_node_specs - Exports the node_specs content of each registered node to files.
 *
 * Iterates over the global node_infos[] array. For each node that has a nonzero
 * node_specs pointer (the EBA buffer address), this function:
 *   1. Looks up the EBA buffer to determine its size.
 *   2. Writes the buffer's content to /var/lib/eba/node_X_specs where X is the node index.
 */
int eba_export_node_specs(void);

int test_eba_utils_file_to_buf(void);
int test_eba_utils_buf_to_file(void);
int test_eba_export_node_specs(void);