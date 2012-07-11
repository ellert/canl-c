#include "canl_locl.h"
#include "canl_mech_ssl.h"
#include <openssl/ocsp.h>

#define USENONCE 0

typedef enum {
    CANL_OCSPRESULT_ERROR_NOTCONFIGURED     = -14,
    CANL_OCSPRESULT_ERROR_NOAIAOCSPURI      = -13,
    CANL_OCSPRESULT_ERROR_INVALIDRESPONSE   = -12,
    CANL_OCSPRESULT_ERROR_CONNECTFAILURE    = -11,
    CANL_OCSPRESULT_ERROR_SIGNFAILURE       = -10,
    CANL_OCSPRESULT_ERROR_BADOCSPADDRESS    = -9,
    CANL_OCSPRESULT_ERROR_OUTOFMEMORY       = -8,
    CANL_OCSPRESULT_ERROR_UNKNOWN           = -7,
    CANL_OCSPRESULT_ERROR_UNAUTHORIZED      = -6,
    CANL_OCSPRESULT_ERROR_SIGREQUIRED       = -5,
    CANL_OCSPRESULT_ERROR_TRYLATER          = -3,
    CANL_OCSPRESULT_ERROR_INTERNALERROR     = -2,
    CANL_OCSPRESULT_ERROR_MALFORMEDREQUEST  = -1,
    CANL_OCSPRESULT_CERTIFICATE_VALID       = 0,
    CANL_OCSPRESULT_CERTIFICATE_REVOKED     = 1
} canl_ocspresult_t;

static int set_ocsp_sign_cert(canl_ocsprequest_t *ocspreq, X509 *sign_cert);
static int set_ocsp_sign_key(canl_ocsprequest_t *ocspreq, EVP_PKEY *sign_key);
static int set_ocsp_cert(canl_ocsprequest_t *ocspreq, X509 *cert);
static int set_ocsp_skew(canl_ocsprequest_t *ocspreq, int skew);
static int set_ocsp_maxage(canl_ocsprequest_t *ocspreq, int maxage);
static int set_ocsp_url(canl_ocsprequest_t *ocspreq, char *url);
static int set_ocsp_issuer(canl_ocsprequest_t *ocspreq, X509 *issuer);
static canl_x509store_t * store_dup(canl_x509store_t *store_from);
static X509_STORE * canl_create_x509store(canl_x509store_t *store);

static OCSP_RESPONSE *send_request(OCSP_REQUEST *req, char *host, char *path,
        int port, int ssl, int req_timeout);

static OCSP_RESPONSE *
query_responder(BIO *conn, char *path, OCSP_REQUEST *req, int req_timeout);

static int
set_ocsp_cert(canl_ocsprequest_t *ocspreq, X509 *cert)
{

    if (!ocspreq)
        ocspreq = calloc(1, sizeof(*ocspreq));
    if (!ocspreq)
        return 1;

    if (cert) {
        if (!ocspreq->cert) {
            X509_free(ocspreq->cert);
            ocspreq->cert = NULL;
        }
        ocspreq->cert = X509_dup(cert);
        if (!ocspreq->cert)
            return 1;
    }
    return 0;
}

    static int 
set_ocsp_url(canl_ocsprequest_t *ocspreq, char *url)
{

    int len = 0;
    if (!ocspreq)
        ocspreq = calloc(1, sizeof(*ocspreq));
    if (!ocspreq)
        return 1;

    if (url) {
        if (!ocspreq->url) {
            free (ocspreq->url);
            ocspreq->url = NULL;
        }
        len = strlen(url);
        ocspreq->url = (char *) malloc((len +1) * sizeof (char));
        if (!ocspreq->url)
            return 1;
        strncpy(ocspreq->url, url, len + 1);
    }
    return 0;
}

    static int 
set_ocsp_issuer(canl_ocsprequest_t *ocspreq, X509 *issuer)
{

    if (!ocspreq)
        ocspreq = calloc(1, sizeof(*ocspreq));
    if (!ocspreq)
        return 1;
    if (issuer) {
        if (!ocspreq->issuer) {
            X509_free (ocspreq->issuer);
            ocspreq->issuer = NULL;
        }
        ocspreq->issuer = X509_dup(issuer);
        if (!ocspreq->issuer)
            return 1;
    }
    return 0;
}

    static int 
set_ocsp_sign_cert(canl_ocsprequest_t *ocspreq, X509 *sign_cert)
{

    if (!ocspreq)
        ocspreq = calloc(1, sizeof(*ocspreq));
    if (!ocspreq)
        return 1;
    if (sign_cert) {
        if (!ocspreq->sign_cert) {
            X509_free (ocspreq->sign_cert);
            ocspreq->sign_cert = NULL;
        }
        ocspreq->sign_cert = X509_dup(sign_cert);
        if (!ocspreq->sign_cert)
            return 1;
    }
    return 0;
}

    static int
set_ocsp_sign_key(canl_ocsprequest_t *ocspreq, EVP_PKEY *sign_key)
{

    if (!ocspreq)
        ocspreq = calloc(1, sizeof(*ocspreq));
    if (!ocspreq)
        return 1;
    if (sign_key) {
        if (!ocspreq->sign_key) {
            EVP_PKEY_free (ocspreq->sign_key);
            ocspreq->sign_key = NULL;
        }
        pkey_dup(&ocspreq->sign_key, sign_key);
        if (!ocspreq->sign_key)
            return 1;
    }
    return 0;
}
    static int
set_ocsp_skew(canl_ocsprequest_t *ocspreq, int skew)
{

    if (!ocspreq)
        ocspreq = calloc(1, sizeof(*ocspreq));
    if (!ocspreq)
        return 1;
    if (skew)
        ocspreq->skew = skew;
    return 0;
}
    static int
set_ocsp_maxage(canl_ocsprequest_t *ocspreq, int maxage)
{

    if (!ocspreq)
        ocspreq = calloc(1, sizeof(*ocspreq));
    if (!ocspreq)
        return 1;
    if (maxage)
        ocspreq->maxage = maxage;
    return 0;
}

static canl_x509store_t * 
store_dup(canl_x509store_t *store_from)
{
    canl_x509store_t *store_to = NULL;
    if (!store_from)
        return NULL;

    store_to = calloc(1, sizeof(*store_to));
    if (!store_to)
        return NULL;

    if (store_from->ca_dir) {
        int len = strlen(store_from->ca_dir);
        store_to->ca_dir = (char *) malloc((len + 1) * sizeof (char));    
        if (store_to->ca_dir)
            return NULL;
        strncpy (store_to->ca_dir, store_from->ca_dir, len + 1);
    }
    if (store_from->crl_dir) {
        int len = strlen(store_from->crl_dir);
        store_to->crl_dir = (char *) malloc((len + 1) * sizeof (char));    
        if (store_to->crl_dir)
            return NULL;
        strncpy (store_to->crl_dir, store_from->crl_dir, len + 1);
    }
    return store_to;
}

    static int
set_ocsp_store(canl_ocsprequest_t *ocspreq, canl_x509store_t *store)
{

    if (!ocspreq)
        ocspreq = calloc(1, sizeof(*ocspreq));
    if (!ocspreq)
        return 1;
    if (store){
        ocspreq->store = store_dup(store);
        if (!ocspreq->store)
            return 1;
    }
    return 0;
}

static X509_STORE *
canl_create_x509store(canl_x509store_t *store)
{
    return NULL;
}

/*TODO error codes in this function has to be passed to canl_ctx somehow*/
/*Timeout shoult be in data structure*/
int do_ocsp_verify (canl_ocsprequest_t *data)
{
    OCSP_REQUEST *req = NULL;
    OCSP_RESPONSE *resp = NULL;
    OCSP_BASICRESP *basic = NULL;
    X509_STORE *store = 0;
    int rc = 0, reason = 0, ssl = 0, status = 0;
    char *host = NULL, *path = NULL, *port = NULL;
    OCSP_CERTID *id = NULL;
    char *chosenurl = NULL;
    canl_ocspresult_t result = 0;
    ASN1_GENERALIZEDTIME  *producedAt, *thisUpdate, *nextUpdate;
    int timeout = -1; // -1 means no timeout - use blocking I/O

    if (!data || !data->cert) { // TODO || !data->issuer ?
        result = EINVAL; //TODO error code
        return result;
    }

    /*get url from cert or use some implicit value*/

    /*get connection parameters out of the chosenurl.
     Determine whether to use encrypted (ssl) connection (based on the url
     format). Url is http[s]://host where host consists of 
     DN [:port] and [path]*/
    if (!OCSP_parse_url(chosenurl, &host, &port, &path, &ssl)) {
        result = CANL_OCSPRESULT_ERROR_BADOCSPADDRESS;
        goto end;
    }
    /*Make new OCSP_REQUEST*/
    if (!(req = OCSP_REQUEST_new())) {
        result = CANL_OCSPRESULT_ERROR_OUTOFMEMORY;
        goto end;
    }

    /*map a cert and its issuer to an ID*/
    id = OCSP_cert_to_id(0, data->cert, data->issuer);

    /* Add an id and nonce to the request*/
    if (!id || !OCSP_request_add0_id(req, id))
        goto end;
    if (USENONCE)
        OCSP_request_add1_nonce(req, 0, -1);

    /* sign the request
       Default hash algorithm is sha1(), might be changed.
       Do not add additional certificates to request
       Do not use flags (e.g. like -no_certs for command line ) now */
    if (data->sign_cert && data->sign_key &&
            !OCSP_request_sign(req, data->sign_cert, data->sign_key, 
                EVP_sha1(), 0, 0)) {
        result = CANL_OCSPRESULT_ERROR_SIGNFAILURE;
        goto end;
    }


    /* establish a connection to the OCSP responder */
    if (!(resp = send_request(req, host, path, atoi(port), ssl, timeout))) {
        result = CANL_OCSPRESULT_ERROR_CONNECTFAILURE;
        goto end;
    }

    /* send the request and get a response */
    if ((rc = OCSP_response_status(resp)) != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
        switch (rc) {
            case OCSP_RESPONSE_STATUS_MALFORMEDREQUEST:
                result = CANL_OCSPRESULT_ERROR_MALFORMEDREQUEST; break;
            case OCSP_RESPONSE_STATUS_INTERNALERROR:
                result = CANL_OCSPRESULT_ERROR_INTERNALERROR;    break;
            case OCSP_RESPONSE_STATUS_TRYLATER:
                result = CANL_OCSPRESULT_ERROR_TRYLATER;         break;
            case OCSP_RESPONSE_STATUS_SIGREQUIRED:
                result = CANL_OCSPRESULT_ERROR_SIGREQUIRED;      break;
            case OCSP_RESPONSE_STATUS_UNAUTHORIZED:
                result = CANL_OCSPRESULT_ERROR_UNAUTHORIZED;     break;
        }
        goto end;
    }

    /* verify the response */
    result = CANL_OCSPRESULT_ERROR_INVALIDRESPONSE;
    if (!(basic = OCSP_response_get1_basic(resp))) 
        goto end;
    if (USENONCE && OCSP_check_nonce(req, basic) <= 0) 
        goto end;
    /*TODO make the store*/ 
    if (data->store && !(store = canl_create_x509store(data->store)))
        goto end;
    /*TODO check the second parametr (responder_cert) and the last one*/
    if ((rc = OCSP_basic_verify(basic, 0, store, 0)) <= 0)
        if ((rc = OCSP_basic_verify(basic, NULL, store, 0)) <= 0)
            goto end;

    if (!OCSP_resp_find_status(basic, id, &status, &reason, &producedAt,
                &thisUpdate, &nextUpdate))
        goto end;
    if (!OCSP_check_validity(thisUpdate, nextUpdate, data->skew, data->maxage))
        goto end;

    /* All done.  Set the return code based on the status from the response. */
    if (status == V_OCSP_CERTSTATUS_REVOKED) {
        result = CANL_OCSPRESULT_CERTIFICATE_REVOKED;
        /*TODO myproxy_log("OCSP status revoked!"); */
    } else {
        result = CANL_OCSPRESULT_CERTIFICATE_VALID;
        /*TODO myproxy_log("OCSP status valid"); */
    }
end:
    /*TODO check what's this 
      if (result < 0 && result != CANL_OCSPRESULT_ERROR_NOTCONFIGURED) {
      ssl_error_to_verror();
      TODO myproxy_log("OCSP check failed");
      myproxy_log_verror();
      } */

    if (host) OPENSSL_free(host);
    if (port) OPENSSL_free(port);
    if (path) OPENSSL_free(path);
    if (req) OCSP_REQUEST_free(req);
    if (resp) OCSP_RESPONSE_free(resp);
    if (basic) OCSP_BASICRESP_free(basic);

    return result;
}

static OCSP_RESPONSE *
send_request(OCSP_REQUEST *req, char *host, char *path,  int port, int ssl,
        int req_timeout) {
    BIO *conn;
    SSL_CTX *ctx_in = NULL;
    OCSP_RESPONSE *resp = NULL;

    if (!(conn = BIO_new_connect(host)))
        goto end;
    BIO_set_conn_int_port(conn, &port);

    if (ssl){
        BIO *sbio;
        /*TODO what method to use? default is SSLv3 for now*/
        ctx_in = SSL_CTX_new(SSLv3_client_method());
        if (ctx_in == NULL) {
            goto end;
        }
        //SSL_CTX_set_cert_store(ctx_in, store);
        /*TODO verify using OCSP? Infinite loop
           !!!!!!!!!!!!!!!!!!!!!!!§
           SSL_CTX_set_mode(ctx_in, SSL_MODE_AUTO_RETRY); ? - return only after
          the handshake and successful completion*/
        SSL_CTX_set_verify(ctx_in, SSL_VERIFY_PEER, NULL);

        sbio = BIO_new_ssl(ctx_in, 1);
        conn = BIO_push(sbio, conn);
        /*
           BIO_get_ssl(conn, &ssl_ptr);

           TODO figure out, how to check cert without canl_ctx

           if (!check_hostname_cert(SSL_get_peer_certificate(ssl_ptr), host))
           goto end;

           TODO verify certs in OCSP at this place? openssl CL tool does not do
           that.
           if (SSL_get_verify_result(ssl_ptr) != X509_V_OK)
           goto end;
         */
    }

    resp = query_responder(conn, path, req, req_timeout);

end:
    if (conn)
        BIO_free_all(conn);
    if (ctx_in)
        SSL_CTX_free(ctx_in);
    return resp;
}

/*TODO the timeout variable should be modified if TO is reached.
  Somehow retur error codes! */
    static OCSP_RESPONSE *
query_responder(BIO *conn, char *path, OCSP_REQUEST *req, int req_timeout)
{
    OCSP_RESPONSE *rsp = NULL;
    
/*openssl does support non blocking BIO for OCSP_send_request*/
#if SSLEAY_VERSION_NUMBER >=  0x0090808fL
    int fd;
    int rv;
    OCSP_REQ_CTX *ctx = NULL;
    fd_set confds;
    struct timeval tv;
    
    /*If timeout is set, the nonblocking I/O flag is set*/
    if (req_timeout != -1)
        BIO_set_nbio(conn, 1);

    rv = BIO_do_connect(conn);
    if ((rv <= 0) && ((req_timeout == -1) || !BIO_should_retry(conn)))
    {
        /*connect failed*/
        return NULL;
    }

    if (BIO_get_fd(conn, &fd) <= 0)
    {
        /*Cannot get the socket*/
        goto err;
    }

    if (req_timeout != -1 && rv <= 0)
    {
        /*try connecting untill timeout is reached*/
        FD_ZERO(&confds);
        FD_SET(fd, &confds);
        tv.tv_usec = 0;
        tv.tv_sec = req_timeout;
        rv = select(fd + 1, NULL, (void *)&confds, NULL, &tv);
        if (rv == 0)
        {
            /*Timeout reached*/
            return NULL;
        }
    }


    /*Prepare OCSP_REQ_CTX*/
    ctx = OCSP_sendreq_new(conn, path, NULL, -1);
    if (!ctx)
        return NULL;
    if (!OCSP_REQ_CTX_set1_req(ctx, req))
        goto err;

    /*send the OCSP request and wait for the response*/
    for (;;)
    {
        rv = OCSP_sendreq_nbio(&rsp, ctx);
        if (rv != -1)
            break;

        /* Blocking I/O flag set
         TODO - might end in an infinite loop? - what about
         SSL_MODE_AUTO_RETRY ?? */
        if (req_timeout == -1) 
            continue;
        FD_ZERO(&confds);
        openssl_fdset(fd, &confds);
        tv.tv_usec = 0;
        tv.tv_sec = req_timeout;
        if (BIO_should_read(conn))
            rv = select(fd + 1, (void *)&confds, NULL, NULL, &tv);
        else if (BIO_should_write(conn))
            rv = select(fd + 1, NULL, (void *)&confds, NULL, &tv);
        else
        {
            /* Unexpected retry condition */
            goto err;
        }
        if (rv == 0)
        {
            /*Timeout on request */
            break;
        }
        if (rv == -1)
        {
            /*Select error*/
            break;
        }

    }

#else
    /*openssl does not support non blocking BIO for OCSP_send_request*/

    if (BIO_do_connect(conn) <= 0)
    {
        /*Error connecting BIO*/
        goto err;
    }

    rsp = OCSP_sendreq_bio(conn, path, req);
    if (!rsp) {
        /*no response from the server*/
        goto err;
    }

#endif

err:
#if SSLEAY_VERSION_NUMBER >=  0x0090808fL
    if (ctx)
        OCSP_REQ_CTX_free(ctx);
#endif 
    return rsp;
}

