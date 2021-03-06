#ifndef _CANL_LOCL_H
#define _CANL_LOCL_H

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <ares.h>
#include <ares_version.h>
#include <netdb.h>
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
#include "canl_err.h"

typedef struct canl_err_desc {
    canl_error code;
    const char *desc;
    unsigned long openssl_lib;
    unsigned long openssl_reason;
} canl_err_desc;

typedef enum canl_err_origin {
    UNKNOWN_ERROR = 0,
    POSIX_ERROR,
    SSL_ERROR,
    CANL_ERROR,
    NETDB_ERROR,
} canl_err_origin;

typedef enum _CANL_AUTH_MECHANISM
{
    AUTH_UNDEF = -1,
    x509 = 0,
    KRB5 = 1, /* and others may be added*/
    TLS,
    GSSAPI,
} CANL_AUTH_MECHANISM;

typedef struct _glb_ctx
{
    char * err_msg;
    canl_err_code err_code;
    /* XXX Do we need to keep these two:? */
    canl_err_origin err_orig;
    long original_err_code;
    void *mech_ctx;
} glb_ctx;

typedef struct _asyn_result {
    struct hostent *ent;
    int err;
} asyn_result;

typedef struct _principal_int {
    char *name;
//    CANL_AUTH_MECHANISM mech_oid;
//    char *raw;  /* e.g. the PEM encoded cert/chain */
} principal_int;

typedef struct _io_handler
{
    int sock;
    principal_int *princ_int;
    void *conn_ctx; //like SSL *
    gss_OID oid;
} io_handler;

typedef struct canl_mech {
    CANL_AUTH_MECHANISM mech;

    canl_err_code (*initialize)
        (glb_ctx *);

    canl_err_code (*set_flags)
        (glb_ctx *cc, unsigned int *mech_flags,  unsigned int flags);

    canl_err_code (*finish)
	(glb_ctx *, void *);

    canl_err_code (*client_init)
        (glb_ctx *, void **);

    canl_err_code (*server_init)
        (glb_ctx *, void **);

    canl_err_code (*free_ctx)
	(glb_ctx *);

    canl_err_code (*connect)
        (glb_ctx *, io_handler *, void *, struct timeval *, const char *);

    canl_err_code (*accept)
        (glb_ctx *, io_handler *, void *, struct timeval *);

    canl_err_code (*close)
        (glb_ctx *, io_handler *, void *);

    size_t (*read)
        (glb_ctx *, io_handler *, void *, void *, size_t, struct timeval *);

    size_t (*write)
        (glb_ctx *, io_handler *, void *, void *, size_t, struct timeval *);

    canl_err_code (*get_peer)
        (glb_ctx *, io_handler *, void *, canl_principal *);

} canl_mech;

/* Mechanism specific */
extern canl_mech canl_mech_ssl;

extern canl_err_desc canl_err_descs[];
extern int canl_err_descs_num;

struct canl_mech *
find_mech(gss_OID oid);


void reset_error (glb_ctx *cc, unsigned long err_code);
canl_err_code set_error (glb_ctx *cc, unsigned long err_code,
	canl_err_origin err_orig, const char *err_format, ...);
canl_err_code update_error (glb_ctx *cc, unsigned long err_code,
	canl_err_origin err_orig, const char *err_format, ...);
void free_hostent(struct hostent *h); //TODO is there some standard funcion to free hostent?
int canl_asyn_getservbyname(int a_family, asyn_result *ares_result,char const *name, 
        struct timeval *timeout);

#endif
