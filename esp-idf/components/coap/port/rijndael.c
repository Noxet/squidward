/*	$OpenBSD: rijndael.c,v 1.19 2008/06/09 07:49:45 djm Exp $ */

/**
 * rijndael-alg-fst.c
 *
 * @version 3.0 (December 2000)
 *
 * Optimised ANSI C code for the Rijndael cipher (now AES)
 *
 * @author Vincent Rijmen <vincent.rijmen@esat.kuleuven.ac.be>
 * @author Antoon Bosselaers <antoon.bosselaers@esat.kuleuven.ac.be>
 * @author Paulo Barreto <paulo.barreto@terra.com.br>
 *
 * This code is hereby placed in the public domain.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ''AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* #include <sys/param.h> */
/* #include <sys/systm.h> */

#include "rijndael.h"

/* setup key context for encryption only */
int
rijndael_set_key_enc_only(rijndael_ctx *ctx, const u_char *key, int bits)
{
	int rounds;

	rounds = rijndaelKeySetupEnc(ctx->ek, key, bits);
	if (rounds == 0)
		return -1;

	ctx->Nr = rounds;
#ifdef WITH_AES_DECRYPT
	ctx->enc_only = 1;
#endif

	return 0;
}

#ifdef WITH_AES_DECRYPT
/* setup key context for both encryption and decryption */
int
rijndael_set_key(rijndael_ctx *ctx, const u_char *key, int bits)
{
	int rounds;

	rounds = rijndaelKeySetupEnc(ctx->ek, key, bits);
	if (rounds == 0)
		return -1;
	if (rijndaelKeySetupDec(ctx->dk, key, bits) != rounds)
		return -1;

	ctx->Nr = rounds;
	ctx->enc_only = 0;

	return 0;
}

void
rijndael_decrypt(rijndael_ctx *ctx, const u_char *src, u_char *dst)
{
	rijndaelDecrypt(ctx->dk, ctx->Nr, src, dst);
}
#endif

void
rijndael_encrypt(rijndael_ctx *ctx, const u_char *src, u_char *dst)
{
	rijndaelEncrypt(ctx->ek, ctx->Nr, src, dst);
}
