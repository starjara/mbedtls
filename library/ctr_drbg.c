/*
 *  CTR_DRBG implementation based on AES-256 (NIST SP 800-90)
 *
 *  Copyright (C) 2006-2011, Brainspark B.V.
 *
 *  This file is part of PolarSSL (http://www.polarssl.org)
 *  Lead Maintainer: Paul Bakker <polarssl_maintainer at polarssl.org>
 *
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 *  The NIST SP 800-90 DRBGs are described in the following publucation.
 *
 *  http://csrc.nist.gov/publications/nistpubs/800-90/SP800-90revised_March2007.pdf
 */

#include "polarssl/config.h"

#if defined(POLARSSL_CTR_DRBG_C)

#include "polarssl/ctr_drbg.h"

int ctr_drbg_init( ctr_drbg_context *ctx,
                   int (*f_entropy)(void *, unsigned char *, size_t),
                   void *p_entropy,
                   unsigned char *custom,
                   size_t len )
{
    int ret;
    unsigned char key[32];

    memset( ctx, 0, sizeof(ctr_drbg_context) );
    memset( key, 0, 32 );

    ctx->f_entropy = f_entropy;
    ctx->p_entropy = p_entropy;

    ctx->entropy_len = CTR_DRBG_ENTROPY_LEN;
    ctx->reseed_interval = CTR_DRBG_RESEED_INTERVAL;

    /*
     * Initialize with an empty key
     */
    aes_setkey_enc( &ctx->aes_ctx, key, 256 );

    if( ( ret = ctr_drbg_reseed( ctx, custom, len ) ) != 0 )
        return( ret );

    return( 0 );
}

void ctr_drbg_set_prediction_resistance( ctr_drbg_context *ctx, int resistance )
{
    ctx->prediction_resistance = resistance;
}

void ctr_drbg_set_entropy_len( ctr_drbg_context *ctx, size_t len )
{
    ctx->entropy_len = len;
}
    
void ctr_drbg_set_reseed_interval( ctr_drbg_context *ctx, int interval )
{
    ctx->reseed_interval = interval;
}
    
int block_cipher_df( unsigned char *output, unsigned char *data, size_t data_len )
{
    unsigned char buf[CTR_DRBG_MAX_INPUT + 32];
    unsigned char tmp[CTR_DRBG_SEEDLEN];
    unsigned char key[32];
    unsigned char chain[16];
    unsigned char *p, *iv;
    aes_context aes_ctx;

    int i, j, buf_len, use_len;

    memset( buf, 0, CTR_DRBG_MAX_INPUT + 32 );

    /*
     * Construct IV (16 bytes) and S in buffer
     * IV = Counter (in 32-bits) padded to 16 with zeroes
     * S = Length input string (in 32-bits) || Length of output (in 32-bits) ||
     *     data || 0x80
     *     (Total is padded to a multiple of 16-bytes with zeroes)
     */
    buf[16] = ( data_len >> 24 ) & 0xff;
    buf[17] = ( data_len >> 16 ) & 0xff;
    buf[18] = ( data_len >> 8  ) & 0xff;
    buf[19] = ( data_len       ) & 0xff;
    buf[23] = CTR_DRBG_SEEDLEN;
    memcpy( buf + 24, data, data_len );
    buf[24 + data_len] = 0x80;

    buf_len = 24 + data_len + 1;

    for( i = 0; i < 32; i++ )
        key[i] = i;

    aes_setkey_enc( &aes_ctx, key, 256 );

    /*
     * Reduce data to POLARSSL_CTR_DRBG_SEEDLEN bytes of data
     */
    for( j = 0; j < CTR_DRBG_SEEDLEN; j += 16 )
    {
        p = buf;
        memset( chain, 0, 16 );
        use_len = buf_len;

        while( use_len > 0 )
        {
            for( i = 0; i < 16; i++ )
                chain[i] ^= p[i];
            p += 16;
            use_len -= 16;

            aes_crypt_ecb( &aes_ctx, AES_ENCRYPT, chain, chain );
        }
        
        memcpy( tmp + j, chain, 16 );

        /*
         * Update IV
         */
        buf[3]++;
    }

    /*
     * Do final encryption with reduced data
     */
    aes_setkey_enc( &aes_ctx, tmp, 256 );
    iv = tmp + 32;
    p = output;

    for( j = 0; j < CTR_DRBG_SEEDLEN; j += 16 )
    {
        aes_crypt_ecb( &aes_ctx, AES_ENCRYPT, iv, iv );
        memcpy( p, iv, 16 );
        p += 16;
    }

    return( 0 );
}

int ctr_drbg_reseed( ctr_drbg_context *ctx, unsigned char *additional, size_t len )
{
    unsigned char seed[CTR_DRBG_MAX_SEED_INPUT];
    size_t seedlen = 0;

    if( ctx->entropy_len + len > CTR_DRBG_MAX_INPUT )
        return( POLARSSL_ERR_CTR_DRBG_INPUT_TOO_BIG );

    memset( seed, 0, CTR_DRBG_MAX_SEED_INPUT );

    /*
     * Gather POLARSSL_CTR_DRBG_ENTROPYLEN bytes of entropy to seed state
     */
    if( 0 != ctx->f_entropy( ctx->p_entropy, seed,
                             ctx->entropy_len ) )
    {
        return( POLARSSL_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED );
    }

    seedlen += ctx->entropy_len;

    /*
     * Add additional data
     */
    if( additional && len )
    {
        memcpy( seed + seedlen, additional, len );
        seedlen += len;
    }

    /*
     * Reduce to 384 bits
     */
    block_cipher_df( seed, seed, seedlen );

    /*
     * Update state
     */
    ctr_drbg_update( ctx, seed );
    ctx->reseed_counter = 1;

    return( 0 );
}
    
int ctr_drbg_update( ctr_drbg_context *ctx,
                     unsigned char data[CTR_DRBG_SEEDLEN] )
{
    unsigned char tmp[CTR_DRBG_SEEDLEN];
    unsigned char *p = tmp;
    int cb, i, j;

    memset( tmp, 0, CTR_DRBG_SEEDLEN );

    for( j = 0; j < CTR_DRBG_SEEDLEN; j += 16 )
    {
        /*
         * Increase counter
         */
        i = 15;
        do {
            ctx->counter[i]++;
            cb = ctx->counter[i] == 0;
        } while( i-- && cb );

        /*
         * Crypt counter block
         */
        aes_crypt_ecb( &ctx->aes_ctx, AES_ENCRYPT, ctx->counter, p );

        p += 16;
    }

    for( i = 0; i < CTR_DRBG_SEEDLEN; i++ )
        tmp[i] ^= data[i];

    /*
     * Update key and counter
     */
    aes_setkey_enc( &ctx->aes_ctx, tmp, 256 );
    memcpy( ctx->counter, tmp + 32, 16 );

    return( 0 );
}

int ctr_drbg_random_with_add( void *p_rng,
                              unsigned char *output, size_t output_len,
                              unsigned char *additional, size_t add_len )
{
    int ret = 0;
    ctr_drbg_context *ctx = (ctr_drbg_context *) p_rng;
    unsigned char add_input[CTR_DRBG_SEEDLEN];
    unsigned char *p = output;
    unsigned char tmp[16];
    int cb, i;

    if( output_len > CTR_DRBG_MAX_REQUEST )
        return( POLARSSL_ERR_CTR_DRBG_REQUEST_TOO_BIG );

    if( add_len > CTR_DRBG_MAX_INPUT )
        return( POLARSSL_ERR_CTR_DRBG_INPUT_TOO_BIG );

    memset( add_input, 0, CTR_DRBG_SEEDLEN );

    if( ctx->reseed_counter > ctx->reseed_interval ||
        ctx->prediction_resistance )
    {
        if( ( ret = ctr_drbg_reseed( ctx, additional, add_len ) ) != 0 )
            return( ret );

        add_len = 0;
    }

    if( add_len > 0 )
    {
        block_cipher_df( add_input, additional, add_len );
        ctr_drbg_update( ctx, add_input );
    }

    while( output_len > 0 )
    {
        /*
         * Increase counter
         */
        i = 15;
        do {
            ctx->counter[i]++;
            cb = ctx->counter[i] == 0;
        } while( i-- && cb );

        /*
         * Crypt counter block
         */
        aes_crypt_ecb( &ctx->aes_ctx, AES_ENCRYPT, ctx->counter, tmp );

        /*
         * Copy random block to destination
         */
        memcpy( p, tmp, (output_len > 16 ) ? 16 : output_len );
        p += 16;
        output_len -= 16;
    }

    ctr_drbg_update( ctx, add_input );

    ctx->reseed_counter++;

    return( 0 );
}

int ctr_drbg_random( void *p_rng, unsigned char *output, size_t output_len )
{
    return ctr_drbg_random_with_add( p_rng, output, output_len, NULL, 0 );
}

#if defined(POLARSSL_SELF_TEST)

#include <stdio.h>

unsigned char entropy_source_pr[96] =
    { 0xc1, 0x80, 0x81, 0xa6, 0x5d, 0x44, 0x02, 0x16,
      0x19, 0xb3, 0xf1, 0x80, 0xb1, 0xc9, 0x20, 0x02,
      0x6a, 0x54, 0x6f, 0x0c, 0x70, 0x81, 0x49, 0x8b,
      0x6e, 0xa6, 0x62, 0x52, 0x6d, 0x51, 0xb1, 0xcb,
      0x58, 0x3b, 0xfa, 0xd5, 0x37, 0x5f, 0xfb, 0xc9,
      0xff, 0x46, 0xd2, 0x19, 0xc7, 0x22, 0x3e, 0x95,
      0x45, 0x9d, 0x82, 0xe1, 0xe7, 0x22, 0x9f, 0x63,
      0x31, 0x69, 0xd2, 0x6b, 0x57, 0x47, 0x4f, 0xa3,
      0x37, 0xc9, 0x98, 0x1c, 0x0b, 0xfb, 0x91, 0x31,
      0x4d, 0x55, 0xb9, 0xe9, 0x1c, 0x5a, 0x5e, 0xe4,
      0x93, 0x92, 0xcf, 0xc5, 0x23, 0x12, 0xd5, 0x56,
      0x2c, 0x4a, 0x6e, 0xff, 0xdc, 0x10, 0xd0, 0x68 };

unsigned char entropy_source_nopr[64] =
    { 0x5a, 0x19, 0x4d, 0x5e, 0x2b, 0x31, 0x58, 0x14,
      0x54, 0xde, 0xf6, 0x75, 0xfb, 0x79, 0x58, 0xfe,
      0xc7, 0xdb, 0x87, 0x3e, 0x56, 0x89, 0xfc, 0x9d,
      0x03, 0x21, 0x7c, 0x68, 0xd8, 0x03, 0x38, 0x20,
      0xf9, 0xe6, 0x5e, 0x04, 0xd8, 0x56, 0xf3, 0xa9,
      0xc4, 0x4a, 0x4c, 0xbd, 0xc1, 0xd0, 0x08, 0x46,
      0xf5, 0x98, 0x3d, 0x77, 0x1c, 0x1b, 0x13, 0x7e,
      0x4e, 0x0f, 0x9d, 0x8e, 0xf4, 0x09, 0xf9, 0x2e };

unsigned char nonce_pers_pr[16] =
    { 0xd2, 0x54, 0xfc, 0xff, 0x02, 0x1e, 0x69, 0xd2,
      0x29, 0xc9, 0xcf, 0xad, 0x85, 0xfa, 0x48, 0x6c };

unsigned char nonce_pers_nopr[16] =
    { 0x1b, 0x54, 0xb8, 0xff, 0x06, 0x42, 0xbf, 0xf5,
      0x21, 0xf1, 0x5c, 0x1c, 0x0b, 0x66, 0x5f, 0x3f };

unsigned char result_pr[16] =
    { 0x34, 0x01, 0x16, 0x56, 0xb4, 0x29, 0x00, 0x8f,
      0x35, 0x63, 0xec, 0xb5, 0xf2, 0x59, 0x07, 0x23 };

unsigned char result_nopr[16] =
    { 0xa0, 0x54, 0x30, 0x3d, 0x8a, 0x7e, 0xa9, 0x88,
      0x9d, 0x90, 0x3e, 0x07, 0x7c, 0x6f, 0x21, 0x8f };

int test_offset;
int ctr_drbg_self_test_entropy( void *p, unsigned char *buf, size_t len )
{
    memcpy( buf, p + test_offset, len );
    test_offset += 32;
    return( 0 );
}

/*
 * Checkup routine
 */
int ctr_drbg_self_test( int verbose )
{
    ctr_drbg_context ctx;
    unsigned char buf[16];

    /*
     * Based on a NIST CTR_DRBG test vector (PR = True)
     */
    if( verbose != 0 )
        printf( "  CTR_DRBG (PR = TRUE) : " );

    test_offset = 0;
    if( ctr_drbg_init( &ctx, ctr_drbg_self_test_entropy, entropy_source_pr, nonce_pers_pr, 16 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }
    ctr_drbg_set_prediction_resistance( &ctx, CTR_DRBG_PR_ON );

    if( ctr_drbg_random( &ctx, buf, 16 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }

    if( ctr_drbg_random( &ctx, buf, 16 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }

    if( memcmp( buf, result_pr, 16 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }
    
    if( verbose != 0 )
        printf( "passed\n" );

    /*
     * Based on a NIST CTR_DRBG test vector (PR = FALSE)
     */
    if( verbose != 0 )
        printf( "  CTR_DRBG (PR = FALSE): " );

    test_offset = 0;
    if( ctr_drbg_init( &ctx, ctr_drbg_self_test_entropy, entropy_source_nopr, nonce_pers_nopr, 16 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }

    if( ctr_drbg_random( &ctx, buf, 16 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }

    if( ctr_drbg_reseed( &ctx, NULL, 0 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }

    if( ctr_drbg_random( &ctx, buf, 16 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }

    if( memcmp( buf, result_nopr, 16 ) != 0 )
    {
        if( verbose != 0 )
            printf( "failed\n" );

        return( 1 );
    }
    
    if( verbose != 0 )
        printf( "passed\n" );

    if( verbose != 0 )
            printf( "\n" );

    return( 0 );
}
#endif

#endif
