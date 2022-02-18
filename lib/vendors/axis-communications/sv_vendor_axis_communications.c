/**
 * MIT License
 *
 * Copyright (c) 2022 Axis Communications AB
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
#include "includes/sv_vendor_axis_communications.h"

#include <stdbool.h>
#include <stdlib.h>  // malloc, memcpy, calloc, free

#include "signed_video_internal.h"
#include "signed_video_tlv.h"
#include "sv_vendor_axis_communications_internal.h"
#include "signed_video_authenticity.h"  // allocate_memory_and_copy_string

#define AXIS_COMMUNICATIONS_NUM_ENCODERS 1
static const sv_tlv_tag_t axis_communications_encoders[AXIS_COMMUNICATIONS_NUM_ENCODERS] = {
    VENDOR_AXIS_COMMUNICATIONS_TAG,
};

typedef struct _sv_vendor_axis_communications_t {
  void *attestation;
  size_t attestation_size;
  char *certificate_chain;
} sv_vendor_axis_communications_t;

void *
sv_vendor_axis_communications_setup(void)
{
  return calloc(1, sizeof(sv_vendor_axis_communications_t));
}

void
sv_vendor_axis_communications_teardown(void *handle)
{
  sv_vendor_axis_communications_t *self = (sv_vendor_axis_communications_t *)handle;
  if (!self) return;

  free(self->attestation);
  free(self->certificate_chain);
  free(self);
}

size_t
encode_axis_communications_handle(void *handle, uint16_t *last_two_bytes, uint8_t *data)
{
  sv_vendor_axis_communications_t *self = (sv_vendor_axis_communications_t *)handle;
  if (!self) return 0;

  size_t data_size = 0;
  const uint8_t version = 1;  // Increment when the change breaks the format

  // Version 1:
  //  - version (1 byte)

  data_size += sizeof(version);
  // Size of attestation report
  data_size += 1;
  data_size += self->attestation_size;

  // Size of certificate chain
  if (self->certificate_chain != NULL) {
    data_size += strlen(self->certificate_chain) + 2;
  } else {
    data_size += 2;
  }

  if (!data) return data_size;

  uint8_t *data_ptr = data;
  uint8_t *attestation = self->attestation;

  // Write version
  write_byte(last_two_bytes, &data_ptr, version, true);
  //
  // Write |certificate_chain|
  if (self->certificate_chain != NULL) {
    write_byte(last_two_bytes, &data_ptr, strlen(self->certificate_chain) + 1, true);
  } else {
    write_byte(last_two_bytes, &data_ptr, 1, true);
  }

  // Write all but the last character.
  if (self->certificate_chain != NULL) {
    write_byte_many(&data_ptr, self->certificate_chain, strlen(self->certificate_chain) + 1, last_two_bytes, true);
  } else {
    write_byte(last_two_bytes, &data_ptr, 0, true);
  }

  write_byte(last_two_bytes, &data_ptr, self->attestation_size, true);
  for (size_t jj = 0; jj < self->attestation_size; ++jj) {
    write_byte(last_two_bytes, &data_ptr, attestation[jj], true);
  }

  return (data_ptr - data);
}

svi_rc
decode_axis_communications_handle(void *handle, const uint8_t *data, size_t data_size)
{
  sv_vendor_axis_communications_t *self = (sv_vendor_axis_communications_t *)handle;
  if (!self) return SVI_INVALID_PARAMETER;

  const uint8_t *data_ptr = data;
  uint8_t version = *data_ptr++;
  uint8_t cert_size = *data_ptr++;

  svi_rc status = SVI_UNKNOWN;
  SVI_TRY()
    SVI_THROW_IF(version == 0, SVI_INCOMPATIBLE_VERSION);

    SVI_THROW(allocate_memory_and_copy_string(&self->certificate_chain, (const char *)data_ptr));
    data_ptr += cert_size;

    self->attestation_size = *data_ptr++;
    if (self->attestation_size > 0 && self->attestation == NULL) {
      self->attestation = malloc(self->attestation_size);
    }

    if (self->attestation_size > 0) {
      memcpy(self->attestation, data_ptr, self->attestation_size);
      data_ptr += self->attestation_size;
    }

    SVI_THROW_IF(data_ptr != data + data_size, SVI_DECODING_ERROR);
  SVI_CATCH()
  SVI_DONE(status)

  return status;
}

SignedVideoReturnCode
sv_vendor_axis_communications_set_attestation_report(signed_video_t *sv,
    void *attestation,
    size_t attestation_size,
    char *certificate_chain)
{
  // Sanity check inputs. It is allowed to set either one of |attestation| and |certificate_chain|,
  // but a mismatch between |attestation| and |attestation_size| returns SV_INVALID_PARAMETER.
  if (!sv) return SV_INVALID_PARAMETER;
  if (!attestation && !certificate_chain) return SV_INVALID_PARAMETER;
  if ((attestation && attestation_size == 0) || (!attestation && attestation_size > 0)) {
    return SV_INVALID_PARAMETER;
  }
  if (!sv->vendor_handle) return SV_NOT_SUPPORTED;

  sv_vendor_axis_communications_t *self = (sv_vendor_axis_communications_t *)sv->vendor_handle;
  bool allocated_attestation = false;
  bool allocated_certificate_chain = false;
  // The user wants to set the |attestation|.
  if (attestation) {
    // If |attestation| already exists, return error.
    if (self->attestation || sv->attestation) return SV_NOT_SUPPORTED;
    // Allocate memory and copy to |self|.
    self->attestation = malloc(attestation_size);
    allocated_attestation = true;
    if (!self->attestation) goto catch_error;
    memcpy(self->attestation, attestation, attestation_size);
    self->attestation_size = attestation_size;

    // Allocate memory and copy to temporary location in |sv|.
    sv->attestation = malloc(attestation_size);
    if (!sv->attestation) goto catch_error;
    memcpy(sv->attestation, attestation, attestation_size);
    sv->attestation_size = attestation_size;
  }

  // The user wants to set the |certificate_chain|.
  if (certificate_chain) {
    // If |certificate_chain| already exists, return error.
    if (self->certificate_chain || sv->certificate_chain) return SV_NOT_SUPPORTED;
    // Allocate memory and copy to |self|.
    self->certificate_chain = calloc(1, strlen(certificate_chain) + 1);
    allocated_certificate_chain = true;
    if (!self->certificate_chain) goto catch_error;
    strcpy(self->certificate_chain, certificate_chain);

    // Allocate memory and copy to temporary location in |sv|.
    sv->certificate_chain = calloc(1, strlen(certificate_chain) + 1);
    if (!sv->certificate_chain) goto catch_error;
    strcpy(sv->certificate_chain, certificate_chain);
  }

  sv->vendor_encoders = axis_communications_encoders;
  sv->num_vendor_encoders = AXIS_COMMUNICATIONS_NUM_ENCODERS;

  return SV_OK;

catch_error:
  if (allocated_attestation) {
    free(self->attestation);
    self->attestation = NULL;
    self->attestation_size = 0;
    free(sv->attestation);
    sv->attestation = NULL;
    sv->attestation_size = 0;
  }
  if (allocated_certificate_chain) {
    free(self->certificate_chain);
    self->certificate_chain = NULL;
    free(sv->certificate_chain);
    sv->certificate_chain = NULL;
  }

  return SV_MEMORY;
}
