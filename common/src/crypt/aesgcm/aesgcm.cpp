//
//  aes-gcm.c
//  Pods
//
//  Created by Markus Kosmal on 20/11/14.
//
//

#include "aesgcm.h"
#include "gcm.h"  

int aes_gcm_encrypt(unsigned char* output, const unsigned char* input, int input_length, const unsigned char* key, const unsigned int key_len, const unsigned char* iv, const unsigned int iv_len,
    unsigned char* tag, unsigned int taglength, const unsigned char* additional, unsigned int additional_length) {

    int ret = 0;                // our return value
    gcm_context ctx;            // includes the AES context structure

    //unsigned int tag_len = 0;
    //unsigned char* tag_buf = NULL;

   //const char* additional = "build-manifest";

    gcm_setkey(&ctx, key, (const uint)key_len);

    ret = gcm_crypt_and_tag(&ctx, ENCRYPT, iv, iv_len, additional, additional_length,
        input, output, input_length, tag, taglength);

    gcm_zero_ctx(&ctx);

    return(ret);
}

int aes_gcm_decrypt(unsigned char* output, const unsigned char* input, int input_length, const unsigned char* key, const unsigned int key_len, const unsigned char* iv, const unsigned int iv_len,
    const unsigned char* tag, unsigned int taglength, const unsigned char* additional, unsigned int additional_length) {

    int ret = 0;                // our return value
    gcm_context ctx;            // includes the AES context structure

    gcm_setkey(&ctx, key, (const uint)key_len);

    //const char* additional = "build-manifest";

    ret = gcm_crypt_and_tag(&ctx, DECRYPT, iv, iv_len, (unsigned char*)additional, additional_length,
        input, output, input_length, (unsigned char*)tag, taglength);

    gcm_zero_ctx(&ctx);

    return(ret);

}