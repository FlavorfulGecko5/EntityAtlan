#pragma once

// Source: https://github.com/mko-x/SharedAES-GCM
// (Note: this library has been tweaked to compile and work properly with Atlan)
//
//  aes-gcm.h
//  MKo
//
//  Created by Markus Kosmal on 20/11/14.
//
//

#ifndef mko_aes_gcm_h
#define mko_aes_gcm_h

extern int gcm_initialize();

int aes_gcm_encrypt(unsigned char* output, const unsigned char* input, int input_length, const unsigned char* key, const unsigned int key_len, const unsigned char* iv, const unsigned int iv_len,
	unsigned char* tag, unsigned int taglength, const unsigned char* additional, unsigned int additional_length);

int aes_gcm_decrypt(unsigned char* output, const unsigned char* input, int input_length, const unsigned char* key, const unsigned int key_len, const unsigned char* iv, const unsigned int iv_len,
	const unsigned char* tag, unsigned int taglength, const unsigned char* additional, unsigned int additional_length);

#endif