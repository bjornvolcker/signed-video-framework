/**
 * MIT License
 *
 * Copyright (c) 2021 Axis Communications AB
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next paragraph) shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __SIGNED_VIDEO_H26X_INTERNAL__
#define __SIGNED_VIDEO_H26X_INTERNAL__

#include <stdbool.h>  // bool

#include "signed_video_defines.h"  // svi_rc
#include "signed_video_internal.h"  // gop_info_t, gop_state_t, HASH_DIGEST_SIZE

typedef struct _h26x_nalu_list_item_t h26x_nalu_list_item_t;
typedef struct _h26x_nalu_t h26x_nalu_t;

#define MAX_PENDING_GOPS 120

typedef enum {
  NALU_TYPE_UNDEFINED = 0,
  NALU_TYPE_SEI = 1,
  NALU_TYPE_I = 2,
  NALU_TYPE_P = 3,
  NALU_TYPE_PS = 4,  // Parameter Set: PPS/SPS/VPS
  NALU_TYPE_OTHER = 5,
} SignedVideoFrameType;

typedef enum {
  UUID_TYPE_UNDEFINED = 0,
  UUID_TYPE_SIGNED_VIDEO = 1,
} SignedVideoUUIDType;

/* Semicolon needed after, ex. DEBUG_LOG("my debug: %d", 42); */
#ifdef SIGNED_VIDEO_DEBUG
char *
nalu_type_to_str(const h26x_nalu_t *nalu);
#endif

/* SEI UUID types */
extern const uint8_t kUuidSignedVideo[UUID_LEN];

/**
 * A struct representing the stream of NALUs, added to Signed Video for validating authenticity.
 * It is a linked list of h26x_nalu_list_item_t and holds the first and last items. The list is
 * linear, that is, one parent and one child only.
 */
struct _h26x_nalu_list_t {
  h26x_nalu_list_item_t *first_item;  // Points to the first item in the linked list, that is, the
  // oldest NALU added for validation.
  h26x_nalu_list_item_t *last_item;  // Points to the last item in the linked list, that is, the
  // latest NALU added for validation.
  int num_items;  // The number of items linked together in the list.

  // Keep pending gop data needed for validation if public_key is late
  gop_state_t gop_state_pending[MAX_PENDING_GOPS];
  gop_info_detected_t gop_info_detected_pending[MAX_PENDING_GOPS];
  int gop_idx;
};

/**
 * A struct representing a NALU in a stream. The stream being a linked list. Each item holds the
 * NALU data as well as pointers to the previous and next items in the list.
 */
struct _h26x_nalu_list_item_t {
  h26x_nalu_t *nalu;  // The parsed NALU information.
  char validation_status;  // The authentication status which can take on the following characters:
  // 'P' : Pending validation. This is the initial value. The NALU has been registered and waiting
  //       for validating the authenticity.
  // 'U' : The NALU has an unknown authenticity. This occurs if the NALU could not be parsed, or if
  //     : the SEI is associated with NALUs not part of the validating segment.
  // '_' : The NALU is ignored and therefore not part of the signature. The NALU has no impact on
  //       the video and can be considered authentic.
  // '.' : The NALU has been validated authentic.
  // 'N' : The NALU has been validated not authentic.
  // 'M' : The validation has detected one or more missing NALUs at this position. Note that
  //       changing the order of NALUs will detect a missing NALU and an invalid NALU.
  // 'E' : An error occurred and validation could not be performed. This should be treated as an
  //       invalid NALU.
  uint8_t hash[HASH_DIGEST_SIZE];  // The hash of the NALU is stored in this memory slot, if it is
  // hashable that is.
  uint8_t *second_hash;  // The hash used for a second verification. Some NALUs, for example the
  // first NALU in a GOP is used in two neighboring GOPs, but with different hashes. The NALU might
  // also require a second verification due to lost NALUs. Memory for this hash is allocated when
  // needed.

  // Flags
  bool taken_ownership_of_nalu;  // Flag to indicate if the item has taken ownership of the |nalu|
  // memory, hence need to free the memory if the item is released.
  bool need_second_verification;  // This NALU need another verification, either due to failures or
  // because it is a chained hash, that is, used in two GOPs. The second verification is done with
  // |second_hash|.
  bool first_verification_not_authentic;  // Marks the NALU as not authentic so the second one does
  // not overwrite with an acceptable status.
  bool has_been_decoded;  // Marks a SEI as decoded. Decoding it twice might overwrite vital
  // information.
  bool used_in_gop_hash;  // Marks the NALU as being part of a computed |gop_hash|.

  // Linked list
  h26x_nalu_list_item_t *prev;  // Points to the previously added NALU. Is NULL if this is the first
  // item.
  h26x_nalu_list_item_t *next;  // Points to the next added NALU. Is NULL if this is the last item.
};

/**
 * Information of a H26x nalu.
 * This struct stores all necessary information of the H26x nalu, such as, pointer to NALU data,
 * NALU data size, pointer to hashable data and size of the hashable data. Further, includes
 * information on NALU type, uuid type (if any) and if the NALU is valid for use/hashing.
 */
struct _h26x_nalu_t {
  const uint8_t *nalu_data;  // The actual NALU data
  size_t nalu_data_size;  // The total size of the NALU data
  const uint8_t *hashable_data;  // The NALU data for potential hashing
  size_t hashable_data_size;  // Size of the data to hash, excluding stop bit
  SignedVideoFrameType nalu_type;  // Frame type: I, P, SPS, PPS, VPS or SEI
  SignedVideoUUIDType uuid_type;  // UUID type if a SEI nalu
  int is_valid;  // Is a valid H26x NALU (1), invalid (0) or has errors (-1)
  bool is_hashable;  // Should be hashed
  const uint8_t *payload;  // Points to the payload (including UUID for SEI-nalus)
  size_t payload_size;  // Parsed payload size
  uint8_t reserved_byte;  // First byte of SEI payload
  const uint8_t *tlv_start_in_nalu_data;  // Points to beginning of the TLV data in the |nalu_data|
  const uint8_t *tlv_data;  // Points to the TLV data after removing emulation prevention bytes
  size_t tlv_size;  // Total size of the |tlv_data|
  uint8_t *tmp_tlv_memory;  // Temporary memory used if there are emulation prevention bytes
  uint32_t start_code;  // Start code or replaced by NALU data size
  int emulation_prevention_bytes;  // Computed emulation prevention bytes
  bool is_primary_slice;  // The first slice in the NALU or not
  bool is_first_nalu_in_gop;  // True for the first slice of an I-frame
  bool is_gop_sei;  // True if this is a Signed Video generated SEI NALU
};

/* Internal APIs for gop_state_t functions */

void
gop_state_print(const gop_state_t *gop_state);

/* Initializes all counters and members of a |gop_state|. */
void
gop_state_init(gop_state_t *gop_state);

/* Initializes all counters and members of |gop_info_detected|. */
void
gop_info_detected_init(gop_info_detected_t *gop_info_detected);

/* Updates the |gop_state| and |gop_info_detected| w.r.t. a |nalu|. */
void
gop_state_update(gop_state_t *gop_state, gop_info_detected_t *gop_info_detected, h26x_nalu_t *nalu);

/* Changes the current |gop_state|, given the new received |nalu|. This is called prior to any
 * actions, since it may involve a necessary reset of the gop_hash. */
void
gop_state_pre_actions(gop_state_t *gop_state, h26x_nalu_t *nalu);

/* Resets the |gop_state| after validating a GOP. The function returns true if a reset of the
 * gop_hash is needed. */
void
gop_state_reset(gop_state_t *gop_state, gop_info_detected_t *gop_info_detected);

/* Others */
void
update_num_nalus_in_gop_hash(signed_video_t *signed_video, const h26x_nalu_t *nalu);

svi_rc
update_gop_hash(gop_info_t *gop_info);

void
check_and_copy_hash_to_hash_list(signed_video_t *signed_video, const uint8_t *nalu_hash);

svi_rc
hash_and_add(signed_video_t *signed_video, const h26x_nalu_t *nalu);

svi_rc
hash_and_add_for_auth(signed_video_t *signed_video, const h26x_nalu_t *nalu);

h26x_nalu_t
parse_nalu_info(const uint8_t *nalu_data,
    size_t nalu_data_size,
    SignedVideoCodec codec,
    bool check_trailing_bytes);

#ifdef SV_UNIT_TEST
/**
 * @brief Sets the recurrence offset for the signed video session
 *
 * Without an offset the recurrent tags are included in the first SEI. But with an offset the
 * recurrent tags are included later dependent on offset.
 *
 * This API is only possible to use when executing unit tests.
 *
 * @param self Session struct pointer
 * @param offset Recurrence offset
 *
 * @returns SV_OK Recurrence offset was successfully set,
 *          SV_INVALID_PARAMETER Invalid parameter,
 *          SV_NOT_SUPPORTED Recurrence interval is not supported.
 */
SignedVideoReturnCode
signed_video_set_recurrence_offset(signed_video_t *self, unsigned offset);
#endif

#endif  // __SIGNED_VIDEO_H26X_INTERNAL__
