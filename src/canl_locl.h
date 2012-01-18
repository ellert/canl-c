#ifndef _CANL_LOCL_H
#define _CANL_LOCL_H


#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <ares.h>
#include <ares_version.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/safestack.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>

#include "sslutils.h"


#include "canl.h"

typedef struct canl_err_desc {
    CANL_ERROR code;
    const char *desc;
    unsigned long openssl_lib;
    unsigned long openssl_reason;
} canl_err_desc;

typedef enum _CANL_ERROR_ORIGIN
{
    unknown_error = 0,
    posix_error = 1,
    ssl_error,
    canl_error,
    netdb_error,
} CANL_ERROR_ORIGIN;

typedef enum _CANL_AUTH_MECHANISM
{
    AUTH_UNDEF = -1,
    x509 = 0,
    KRB5 = 1, /* and others may be added*/
    TLS,
    GSSAPI,
} CANL_AUTH_MECHANISM;

typedef struct _cert_key_store {
    X509 *cert;
    EVP_PKEY *key;
} cert_key_store;

typedef struct _glb_ctx
{
    char * err_msg;
    unsigned long err_code;
    CANL_ERROR_ORIGIN err_orig;
    cert_key_store *cert_key;
} glb_ctx;

typedef struct _asyn_result {
    struct hostent *ent;
    int err;
} asyn_result;

typedef struct _principal_int {
    char *name;
    CANL_AUTH_MECHANISM mech_oid;
    char *raw;  /* e.g. the PEM encoded cert/chain */
} principal_int;

typedef struct _io_handler
{
    int sock;
    principal_int *princ_int;
    struct authn_mech {
	CANL_AUTH_MECHANISM type;
	gss_OID oid;
	void *ctx;
    } authn_mech;
} io_handler;

typedef struct canl_mech {
    CANL_AUTH_MECHANISM mech;
    void *global_context;

    canl_err_code (*initialize)
        (glb_ctx *, void **);

    canl_err_code (*finish)
	(glb_ctx *, void *);

    canl_err_code (*client_init)
        (glb_ctx *, void *, void **);

    canl_err_code (*server_init)
        (glb_ctx *, void *, void **);

    canl_err_code (*free_ctx)
	(glb_ctx *, void *);

    canl_err_code (*connect)
        (glb_ctx *, io_handler *, void *, struct timeval *, const char *);

    canl_err_code (*accept)
        (glb_ctx *, io_handler *, void *, struct timeval *);

    canl_err_code (*close)
        (glb_ctx *, io_handler *, void *);

    canl_err_code (*read)
        (glb_ctx *, io_handler *, void *, void *, size_t, struct timeval *);

    canl_err_code (*write)
        (glb_ctx *, io_handler *, void *, void *, size_t, struct timeval *);
} canl_mech;

struct canl_mech *
find_mech(gss_OID oid);

extern struct canl_mech canl_mech_ssl;

void reset_error (glb_ctx *cc, unsigned long err_code);
int set_error (glb_ctx *cc, unsigned long err_code, CANL_ERROR_ORIGIN err_orig,
        const char *err_format, ...);
int update_error (glb_ctx *cc, unsigned long err_code, CANL_ERROR_ORIGIN err_orig,
	const char *err_format, ...);
void free_hostent(struct hostent *h); //TODO is there some standard funcion to free hostent?
int asyn_getservbyname(int a_family, asyn_result *ares_result,char const *name, 
        struct timeval *timeout);

/*TODO maybe move to another haeder file*/
int do_set_ctx_own_cert_file(glb_ctx *cc, char *cert, char *key);
int set_key_file(glb_ctx *cc, EVP_PKEY **to, const char *key);
int set_cert_file(glb_ctx *cc, X509 **to, const char *cert);

#endif
