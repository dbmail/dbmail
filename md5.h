#ifndef GdmMD5_H
#define GdmMD5_H

#ifdef __alpha
typedef unsigned int uint32;
#else
typedef unsigned long uint32;
#endif

struct GdmMD5Context {
	uint32 buf[4];
	uint32 bits[2];
	unsigned char in[64];
};

void gdm_md5_init (struct GdmMD5Context *context);
void gdm_md5_update (struct GdmMD5Context *context, unsigned char const *buf,
		     unsigned len);
void gdm_md5_final (unsigned char digest[16], struct GdmMD5Context *context);
void gdm_md5_transform (uint32 buf[4], uint32 const in[16]);

/*
 * This is needed to make RSAREF happy on some MS-DOS compilers.
 */
/* typedef struct gdm_md5_Context gdm_md5__CTX; */

#endif /* !GdmMD5_H */
