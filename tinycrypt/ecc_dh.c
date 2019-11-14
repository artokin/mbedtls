/* ec_dh.c - TinyCrypt implementation of EC-DH */

/*
 *  Copyright (c) 2019, Arm Limited (or its affiliates), All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Copyright (c) 2014, Kenneth MacKay
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  Copyright (C) 2017 by Intel Corporation, All Rights Reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *    - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *    - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *    - Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_USE_TINYCRYPT)
#include <tinycrypt/ecc.h>
#include <tinycrypt/ecc_dh.h>
#include <string.h>
#include "mbedtls/platform_util.h"

int uECC_make_key_with_d(uint8_t *public_key, uint8_t *private_key,
			 unsigned int *d, uECC_Curve curve)
{

	uECC_word_t _private[NUM_ECC_WORDS];
	uECC_word_t _public[NUM_ECC_WORDS * 2];

	/* This function is designed for test purposes-only (such as validating NIST
	 * test vectors) as it uses a provided value for d instead of generating
	 * it uniformly at random. */
	mbedtls_platform_memcpy (_private, d, NUM_ECC_BYTES);

	/* Computing public-key from private: */
	if (EccPoint_compute_public_key(_public, _private, curve)) {

		/* Converting buffers to correct bit order: */
		uECC_vli_nativeToBytes(private_key,
				       BITS_TO_BYTES(curve->num_n_bits),
				       _private);
		uECC_vli_nativeToBytes(public_key,
				       curve->num_bytes,
				       _public);
		uECC_vli_nativeToBytes(public_key + curve->num_bytes,
				       curve->num_bytes,
				       _public + curve->num_words);

		/* erasing temporary buffer used to store secret: */
		mbedtls_platform_memset(_private, 0, NUM_ECC_BYTES);

		return 1;
	}
	return 0;
}

int uECC_make_key(uint8_t *public_key, uint8_t *private_key, uECC_Curve curve)
{

	uECC_word_t _random[NUM_ECC_WORDS * 2];
	uECC_word_t _private[NUM_ECC_WORDS];
	uECC_word_t _public[NUM_ECC_WORDS * 2];
	uECC_word_t tries;

	for (tries = 0; tries < uECC_RNG_MAX_TRIES; ++tries) {
		/* Generating _private uniformly at random: */
		uECC_RNG_Function rng_function = uECC_get_rng();
		if (!rng_function ||
			!rng_function((uint8_t *)_random, 2 * NUM_ECC_WORDS*uECC_WORD_SIZE)) {
        		return 0;
		}

		/* computing modular reduction of _random (see FIPS 186.4 B.4.1): */
		uECC_vli_mmod(_private, _random, curve->n);

		/* Computing public-key from private: */
		if (EccPoint_compute_public_key(_public, _private, curve)) {

			/* Converting buffers to correct bit order: */
			uECC_vli_nativeToBytes(private_key,
					       BITS_TO_BYTES(curve->num_n_bits),
					       _private);
			uECC_vli_nativeToBytes(public_key,
					       curve->num_bytes,
					       _public);
			uECC_vli_nativeToBytes(public_key + curve->num_bytes,
 					       curve->num_bytes,
					       _public + curve->num_words);

			/* erasing temporary buffer that stored secret: */
			mbedtls_platform_memset(_private, 0, NUM_ECC_BYTES);

      			return 1;
    		}
  	}
	return 0;
}

int uECC_shared_secret(const uint8_t *public_key, const uint8_t *private_key,
		       uint8_t *secret, uECC_Curve curve)
{

	uECC_word_t _public[NUM_ECC_WORDS * 2];
	uECC_word_t _private[NUM_ECC_WORDS];
	wordcount_t num_words = curve->num_words;
	wordcount_t num_bytes = curve->num_bytes;
	int r;

        /* Protect against invalid curve attacks */
        if (uECC_valid_public_key(public_key, curve) != 0) {
            r = 0;
            goto clear_and_out;
        }

	/* Converting buffers to correct bit order: */
	uECC_vli_bytesToNative(_private,
      			       private_key,
			       BITS_TO_BYTES(curve->num_n_bits));
	uECC_vli_bytesToNative(_public,
      			       public_key,
			       num_bytes);
	uECC_vli_bytesToNative(_public + num_words,
			       public_key + num_bytes,
			       num_bytes);

	r = EccPoint_mult_safer(_public, _public, _private, curve);
        if (r == 0)
            goto clear_and_out;

	uECC_vli_nativeToBytes(secret, num_bytes, _public);
	r = !EccPoint_isZero(_public, curve);

clear_and_out:
	/* erasing temporary buffer used to store secret: */
	mbedtls_platform_zeroize(_private, sizeof(_private));

	return r;
}
#else
typedef int mbedtls_dummy_tinycrypt_def;
#endif /* MBEDTLS_USE_TINYCRYPT */
