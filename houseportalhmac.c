/* houseport - A simple Web portal for home servers.
 *
 * Copyright 2020, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * houseportalhmac.c - Calculate a cryptographic signature of a short text.
 *
 * This module is based on OpenSSL
 *
 * SYNOPSYS:
 *
 * const char *houseportalhmac (const char *cypher,
 *                              const char *hexkey, const char *data);
 *
 *    Return a signature as a hex string (static), or null if error.
 *
 * int houseportalhmac_size (const char *cypher);
 *
 *    Return the size of a signature for the specified cypher.
 *
 * LIMITATIONS:
 *
 * Only supports SHA-256 for the time being.
 */

#include <ctype.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "houseportalhmac.h"

static char bin2hex (int value) {
    static const char bin2heximage[] = "0123456789abcdef";
    return bin2heximage[value&0x0f];
}

static int hex2bin (char value) {
    if (isdigit(value)) {
        return value - '0';
    }
    if (isxdigit(value)) {
        if (isupper(value))
            return value - 'A' + 10;
        return value - 'a' + 10;
    }
    return 0; // Error: not a valid hex character.
}

static int hmac_hex2bin (const char *hex, unsigned char *bin, int size) {

    int i;
    int length = strlen(hex);

    length = length & (~1); // Force even length by truncating.
    if (length > 2 * size) length = 2 * size;

    for (i = 0; i <= length; i += 2) {
        bin[i/2] = (char)(hex2bin(hex[i+1]) + 16 * hex2bin(hex[i]));
    }
    return length / 2;
}

const char *houseportalhmac (const char *cypher,
                             const char *hexkey, const char *data) {

    if (strcmp(cypher, "SHA-256") == 0) {

        unsigned char key[64];
        unsigned char output[EVP_MAX_MD_SIZE];
        unsigned int outlen = EVP_MAX_MD_SIZE;
        int keylen = hmac_hex2bin (hexkey, key, sizeof(key));

        unsigned char *result =
            HMAC(EVP_sha256(), key, keylen, (unsigned char *)data, strlen(data), output, &outlen);
        if (result) {
            static char signature[9];
            int i;
            if (outlen > 4) outlen = 4;
            for (i = 0; i < outlen; ++i) {
                signature[2*i] = bin2hex(output[i]>>4);
                signature[2*i+1] = bin2hex(output[i]);
            }
            signature[8] = 0;
            return signature;
        }
        return 0;
    }
    return 0;
}

int houseportalhmac_size (const char *cypher) {

    if (strcmp(cypher, "SHA-256") == 0) {
        return 8; // See above.
    }
    return 0;
}

