#ifndef PROTON_CONSUMERS_H
#define PROTON_CONSUMERS_H

/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

/* Definitions of AMQP type codes */
#include "encodings.h"

#include "proton/codec.h"
#include "proton/types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct pni_consumer_t {
  const uint8_t* output_start;
  size_t size;
  size_t position;
} pni_consumer_t;

static inline pni_consumer_t make_consumer_from_bytes(pn_bytes_t output_bytes) {
  return (pni_consumer_t){
    .output_start = (const uint8_t*) output_bytes.start,
    .size = output_bytes.size,
    .position = 0
  };
}

static inline bool pni_consumer_readf8(pni_consumer_t *consumer, uint8_t* result)
{
  if (consumer->position+1 > consumer->size) {
    consumer->position = consumer->size;
    return false;
  }
  uint8_t r = consumer->output_start[consumer->position+0];
  consumer->position++;
  *result = r;
  return true;
}

static inline bool pni_consumer_readf16(pni_consumer_t *consumer, uint16_t* result)
{
  if (consumer->position+2 > consumer->size) {
    consumer->position = consumer->size;
    return false;
  }
  uint16_t a = consumer->output_start[consumer->position+0];
  uint16_t b = consumer->output_start[consumer->position+1];
  uint16_t r = a << 8
  | b;
  consumer->position += 2;
  *result = r;
  return true;
}

static inline bool pni_consumer_readf32(pni_consumer_t *consumer, uint32_t* result)
{
  if (consumer->position+4 > consumer->size) {
    consumer->position = consumer->size;
    return false;
  }
  uint32_t a = consumer->output_start[consumer->position+0];
  uint32_t b = consumer->output_start[consumer->position+1];
  uint32_t c = consumer->output_start[consumer->position+2];
  uint32_t d = consumer->output_start[consumer->position+3];
  uint32_t r = a << 24
  | b << 16
  | c <<  8
  | d;
  consumer->position += 4;
  *result = r;
  return true;
}

static inline bool pni_consumer_readf64(pni_consumer_t *consumer, uint64_t* result)
{
  uint32_t a;
  if (!pni_consumer_readf32(consumer, &a)) return false;
  uint32_t b;
  if (!pni_consumer_readf32(consumer, &b)) return false;
  *result = (uint64_t)a << 32 | (uint64_t)b;
  return true;
}

static inline bool pni_consumer_readf128(pni_consumer_t *consumer, void *dst)
{
  if (consumer->position+16 > consumer->size) return false;

  memcpy(dst, &consumer->output_start[consumer->position], 16);
  consumer->position += 16;
  return true;
}

static inline bool pni_consumer_read_value_not_described(pni_consumer_t* consumer, uint8_t type, pn_bytes_t *value) {
  uint8_t subcategory = type >> 4;
  switch (subcategory) {
    // Fixed width types:
    // No data
    case 0x4:
      *value = (pn_bytes_t){.size=0,.start=NULL};
      return true;
      // 1 Octet
    case 0x5:
      if (consumer->position+1 > consumer->size) break;
      *value = (pn_bytes_t){.size=1,.start=(const char *)consumer->output_start+consumer->position};
      consumer->position += 1;
      return true;
      // 2 Octets
    case 0x6:
      if (consumer->position+2 > consumer->size) break;
      *value = (pn_bytes_t){.size=2,.start=(const char *)consumer->output_start+consumer->position};
      consumer->position += 2;
      return true;
      // 4 Octets
    case 0x7:
      if (consumer->position+4 > consumer->size) break;
      *value = (pn_bytes_t){.size=4,.start=(const char *)consumer->output_start+consumer->position};
      consumer->position += 4;
      return true;
      // 8 Octets
    case 0x8:
      if (consumer->position+8 > consumer->size) break;
      *value = (pn_bytes_t){.size=8,.start=(const char *)consumer->output_start+consumer->position};
      consumer->position += 8;
      return true;
      // 16 Octets
    case 0x9:
      if (consumer->position+16 > consumer->size) break;
      *value = (pn_bytes_t){.size=16,.start=(const char *)consumer->output_start+consumer->position};
      consumer->position += 16;
      return true;
      // Variable width types:
      // One Octet of size
    case 0xA:
    case 0xC:
    case 0xE: {
      uint8_t size;
      if (!pni_consumer_readf8(consumer, &size)) return false;
      if (consumer->position+size > consumer->size) break;
      *value = (pn_bytes_t){.size=size,.start=(const char *)consumer->output_start+consumer->position};
      consumer->position += size;
      return true;
    }
    // 4 Octets of size
    case 0xB:
    case 0xD:
    case 0xF: {
      uint32_t size;
      if (!pni_consumer_readf32(consumer, &size)) return false;
      if (consumer->position+size > consumer->size) break;
      *value = (pn_bytes_t){.size=size,.start=(const char *)consumer->output_start+consumer->position};
      consumer->position += size;
      return true;
    }
    default:
      break;
  }
  consumer->position = consumer->size;
  return false;
}

///////////////////////////////////////////////////////////////////////////////

static inline bool consume_expected_ubyte(pni_consumer_t* consumer, uint8_t expected)
{
    uint8_t e;
    return pni_consumer_readf8(consumer, &e) && e==expected;
}

static inline bool consume_ulong(pni_consumer_t* consumer, uint64_t *ulong) {
  uint8_t type;
  if (!pni_consumer_readf8(consumer, &type)) return false;
  switch (type) {
    case PNE_SMALLULONG: {
      uint8_t ul;
      if (!pni_consumer_readf8(consumer, &ul)) return false;
      *ulong = ul;
      break;
    }
    case PNE_ULONG: {
      uint64_t ul;
      if (!pni_consumer_readf64(consumer, &ul)) return false;
      *ulong = ul;
      break;
    }
    case PNE_ULONG0: {
      *ulong = 0;
      break;
    }
    default:
      return false;
  }
  return true;
}

// XXX: assuming numeric -
// if we get a symbol we should map it to the numeric value and dispatch on that
static inline bool consume_descriptor(pni_consumer_t* consumer, uint64_t *descriptor) {
  return
    consume_expected_ubyte(consumer, PNE_DESCRIPTOR) &&
    consume_ulong(consumer, descriptor);
}

#endif // PROTON_CONSUMERS_H
