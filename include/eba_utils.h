/**
 * @file eba_utils.h
 * @brief EBA Utilities for File and Buffer Operations.
 *
 * This header provides utility functions to facilitate transferring data
 * between files on disk and EBA buffers. It includes routines to read an entire
 * file into a pre-allocated buffer, write the contents of a buffer to a file, and
 * export node-specific data to disk. Additionally, test functions are provided to
 * verify the correctness of these operations.
 */

#include "eba_internals.h"   
#include "eba.h"             
#include "ebp.h" 

/**
 * eba_utils_file_to_buf - Read an entire file into a pre-allocated EBA buffer.
 * @filepath: Absolute path to the file on disk (e.g. "/var/lib/eba/somefile.eba").
 * @buff_id:  Identifier of the pre-allocated buffer.
 *
 * This function reads the complete contents of the specified file and copies it
 * into the EBA buffer identified by @buff_id.
 *
 * Return: 0 on success, 1 on error.
 */
int eba_utils_file_to_buf(const char *filepath, uint64_t buff_id);

/**
 * eba_utils_buf_to_file - Write the entire contents of an EBA buffer to a file.
 * @buff_id:  Identifier of the EBA buffer containing the data.
 * @size:     The size of the data in the buffer.
 * @filepath: Absolute path to the destination file on disk (e.g. "/var/lib/eba/dump.eba").
 *
 * This function writes the content of the EBA buffer, starting from the beginning
 * and spanning @size bytes, to the file specified by @filepath.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int eba_utils_buf_to_file(uint64_t buff_id, uint64_t size, const char *filepath);


/**
 * eba_export_node_specs - Export node specification buffers to files.
 *
 * Iterates over the global node_infos[] array and for each node that has a nonzero
 * node_specs pointer (i.e. an EBA buffer address), this function:
 *
 *  1. Determines the size of the EBA buffer.
 *  2. Writes the contents of the buffer to a file located at
 *     /var/lib/eba/node_X_specs, where X is the node index.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int eba_export_node_specs(void);

/**
 * test_eba_utils_file_to_buf - Self-test for eba_utils_file_to_buf function.
 *
 * Return: 0 on success, or a non-zero error code on failure.
 */
int test_eba_utils_file_to_buf(void);

/**
 * test_eba_utils_buf_to_file - Self-test for eba_utils_buf_to_file function.
 *
 * Return: 0 on success, or a non-zero error code on failure.
 */
int test_eba_utils_buf_to_file(void);


/**
 * test_eba_export_node_specs - Self-test for eba_export_node_specs function.
 *
 * Return: 0 on success, or a non-zero error code on failure.
 */
int test_eba_export_node_specs(void);