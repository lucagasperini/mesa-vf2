/*
 * Copyright © 2023 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_csb.h"
#include "pvr_device.h"
#include "pvr_device_info.h"
#include "pvr_hw_pass.h"
#include "pvr_macros.h"
#include "pvr_pass.h"
#include "pvr_physical_device.h"
#include "pvr_spm.h"
#include "util/macros.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"
#include "vk_alloc.h"
#include "vk_log.h"


static VkResult
pvr_spm_scratch_buffer_alloc(struct pvr_device *device,
                             uint64_t size,
                             struct pvr_spm_scratch_buffer **const buffer_out)
{
   const uint32_t cache_line_size =
      pvr_get_slc_cache_line_size(&device->pdevice->dev_info);
   struct pvr_spm_scratch_buffer *scratch_buffer;
   struct pvr_suballoc_bo *bo;
   VkResult result;
   result = pvr_bo_suballoc(&device->suballoc_general,
                         size,
                         cache_line_size,
                         0,
                         &bo);
   if (result != VK_SUCCESS) {
      *buffer_out = NULL;
      return result;
   }

   scratch_buffer = vk_alloc(&device->vk.alloc,
                             sizeof(*scratch_buffer),
                             4,
                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!scratch_buffer) {
      pvr_bo_suballoc_free(bo);
      *buffer_out = NULL;
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   *scratch_buffer = (struct pvr_spm_scratch_buffer){
      .bo = bo,
      .size = size,
   };

   *buffer_out = scratch_buffer;

   return VK_SUCCESS;
}


void pvr_spm_scratch_buffer_release(struct pvr_device *device,
                                    struct pvr_spm_scratch_buffer *buffer)
{
   if (p_atomic_dec_zero(&buffer->ref_count)) {
      pvr_bo_suballoc_free(buffer->bo);
      vk_free(&device->vk.alloc, buffer);
   }
}


VkResult pvr_spm_scratch_buffer_get_buffer(
   struct pvr_device *device,
   uint64_t size,
   struct pvr_spm_scratch_buffer **const buffer_out)
{
   struct pvr_spm_scratch_buffer *buffer;

   VkResult result;

   result = pvr_spm_scratch_buffer_alloc(device, size, &buffer);
   if (result != VK_SUCCESS) {
      *buffer_out = NULL;
      return result;
   }

   p_atomic_inc(&buffer->ref_count);
   *buffer_out = buffer;

   return VK_SUCCESS;
}

void pvr_spm_finish_eot_state(struct pvr_device *device,
                              struct pvr_spm_eot_state *spm_eot_state)
{
   pvr_bo_suballoc_free(spm_eot_state->pixel_event_program_data_upload);
   pvr_bo_suballoc_free(spm_eot_state->usc_eot_program);
}

void pvr_spm_finish_bgobj_state(struct pvr_device *device,
                                struct pvr_spm_bgobj_state *spm_bgobj_state)
{
   pvr_bo_suballoc_free(spm_bgobj_state->pds_texture_data_upload);
   pvr_bo_suballoc_free(spm_bgobj_state->consts_buffer);
}
