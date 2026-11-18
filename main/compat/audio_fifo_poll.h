#pragma once

#include "esp_gmf_fifo.h"

#ifdef __cplusplus
extern "C" {
#endif

// A bounded recorder poll is expected to time out while idle. Other callers
// retain esp_gmf_fifo_acquire_read() and its normal timeout error reporting.
esp_gmf_err_io_t audio_ownership_fifo_acquire_read_quiet(
    esp_gmf_fifo_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks);

#ifdef __cplusplus
}
#endif
