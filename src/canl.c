#include "canl_locl.h"

#define tv_sub(a,b) {\
    (a).tv_usec -= (b).tv_usec;\
    (a).tv_sec -= (b).tv_sec;\
    if ((a).tv_usec < 0) {\
        (a).tv_sec--;\
        (a).tv_usec += 1000000;\
    }\
}

static struct canl_mech *mechs[] = {
    &canl_mech_ssl,
};

static void io_destroy(glb_ctx *cc, io_handler *io);
static int init_io_content(glb_ctx *cc, io_handler *io);
static int try_connect(glb_ctx *glb_cc, io_handler *io_cc, char *addr,
        int addrtype, int port, struct timeval *timeout);

canl_ctx canl_create_ctx()
{
    glb_ctx *ctx = NULL;
    int  i;

    /*create context*/
    ctx = (glb_ctx *) calloc(1, sizeof(*ctx));
    if (!ctx) 
        return NULL;

    for (i = 0; i < sizeof(mechs)/sizeof(mechs[0]); i++)
	mechs[i]->initialize(ctx); /*TODO every mech must have its own ctx*/

    return ctx;
}

void canl_free_ctx(canl_ctx cc)
{
    glb_ctx *ctx = (glb_ctx*) cc;
    struct canl_mech *mech = find_mech(GSS_C_NO_OID);

    if (!cc)
        return;

    /*delete content*/
    if (ctx->err_msg) {
        free(ctx->err_msg);
        ctx->err_msg = NULL;
    }
    /*TODO delete ctx content for real*/
    if (mech)
        mech->free_ctx(ctx);

    if (ctx->err_msg){
        free(ctx->err_msg);
        ctx->err_msg = NULL;
    }
    free(ctx);
}


canl_err_code
canl_create_io_handler(canl_ctx cc, canl_io_handler *io)
{
    io_handler *new_io_h = NULL;
    glb_ctx *g_cc = cc;
    int err = 0;

    if (!g_cc) 
        return EINVAL;
    if (!io)
        return set_error(g_cc, EINVAL, POSIX_ERROR, "IO handler not"
                " initialized");
        
    /*create io handler*/
    new_io_h = (io_handler *) calloc(1, sizeof(*new_io_h));
    if (!new_io_h)
        return set_error(g_cc, ENOMEM, POSIX_ERROR, "Not enough memory");

    /* allocate memory and initialize io content*/
    if ((err = init_io_content(g_cc, new_io_h))){
	free(new_io_h);
	return err;
    }

    *io = new_io_h;
    return 0;
}

static int init_io_content(glb_ctx *cc, io_handler *io)
{
    io->oid = GSS_C_NO_OID;
    io->sock = -1;
    return 0;
}

canl_err_code
canl_io_connect(canl_ctx cc, canl_io_handler io, const char *host, 
        const char *service, int port, gss_OID_set auth_mechs, 
        int flags, canl_principal *peer, struct timeval *timeout)
{
    int err = 0;
    io_handler *io_cc = (io_handler*) io;
    glb_ctx *glb_cc = (glb_ctx*) cc;
    struct _asyn_result ar;
    int i = 0, k;
    int addr_types[] = {AF_INET, AF_INET6}; //TODO ip versions policy?
    int ipver = AF_INET6;
    int j = 0, done;
    struct canl_mech *mech;
    gss_OID oid;

    memset(&ar, 0, sizeof(ar));

    if (!glb_cc) {
        return EINVAL;
    }

    if (!io_cc)
        return set_error(glb_cc, EINVAL, POSIX_ERROR, 
                "IO handler not initialized");

    done = 0;
    for (k = 0; k < sizeof(addr_types)/sizeof(*addr_types); k++) {
        ipver = addr_types[k];
	if (ar.ent) {
	    free_hostent(ar.ent);
	    memset(&ar, 0, sizeof(ar));
	}

        ar.ent = (struct hostent *) calloc (1, sizeof(struct hostent));
        if (ar.ent == NULL)
            return set_error(cc, ENOMEM, POSIX_ERROR, "Not enough memory");

        switch (err = canl_asyn_getservbyname(ipver, &ar, host, NULL)) {
            case NETDB_SUCCESS:
                err = 0;
                break;
            case TRY_AGAIN:
                err = update_error(glb_cc, ETIMEDOUT, POSIX_ERROR,
                        " Timeout reached when connecting to (%s)", host);
		goto end;
            case NETDB_INTERNAL:
		err = update_error(glb_cc, errno, POSIX_ERROR,
                        "Cannot resolve the server hostname (%s)", host);
                continue;
            default:
                err = update_error(glb_cc, err, NETDB_ERROR,
                        "Cannot resolve the server hostname (%s)", host);
                continue;
        }

	j = 0;
	do {
	    if (auth_mechs == GSS_C_NO_OID_SET || auth_mechs->count == 0)
		oid = GSS_C_NO_OID;
	    else
		oid = &auth_mechs->elements[j];

	    mech = find_mech(oid);

	    err = 0;
	    for (i = 0; ar.ent->h_addr_list[i]; i++) {
		void *ctx = NULL;

		if (err == ETIMEDOUT)
		    goto end;

		err = try_connect(glb_cc, io_cc, ar.ent->h_addr_list[i], 
			ar.ent->h_addrtype, port, timeout);//TODO timeout
		if (err)
		    continue;

		err = mech->client_init(glb_cc, &ctx);
		if (err) {
		    canl_io_close(glb_cc, io_cc);
		    continue;
		}

		err = mech->connect(glb_cc, io_cc, ctx, timeout, host);
		if (err) {
		    canl_io_close(glb_cc, io_cc);
		    mech->finish(glb_cc, ctx);
		    ctx = NULL;
                    continue;
                }
                io_cc->conn_ctx = ctx;
                done = 1;
                /* If peer != NULL then client certificate is mandatory*/
                if (peer) {
                    err = mech->get_peer(glb_cc, io_cc, ctx, peer);
                    if (err)
                        goto end;
                }
                
                break;
	    }
	    if (err == ETIMEDOUT)
		goto end;
	    j++;
	} while (auth_mechs != GSS_C_NO_OID_SET && j < auth_mechs->count && !done);

        free_hostent(ar.ent);
        ar.ent = NULL;
	if (done)
	    break;
    }

    if (!done) {
	err = ECONNREFUSED;
	goto end;
    }

    err = 0;

end:
    if (err) /* XXX: rather invent own error */
	err = update_error(glb_cc, ECONNREFUSED, POSIX_ERROR,
		"Failed to make network connection to server %s", host);

    if (ar.ent != NULL)
        free_hostent(ar.ent);

    return err;
}
/* try to connect to addr with port (both ipv4 and 6)
 * return 0 when successful
 * errno otherwise*/
/* XXX use set_error on errors and return a CANL return code */
static int try_connect(glb_ctx *glb_cc, io_handler *io_cc, char *addr,
        int addrtype, int port, struct timeval *timeout)
{
    struct sockaddr_storage a;
    struct sockaddr_storage *p_a=&a;
    socklen_t a_len;
    socklen_t err_len;
    int sock;
    int sock_err;

    struct timeval before,after,to;

    struct sockaddr_in *p4 = (struct sockaddr_in *)p_a;
    struct sockaddr_in6 *p6 = (struct sockaddr_in6 *)p_a;

    memset(p_a, 0, sizeof *p_a);
    p_a->ss_family = addrtype;
    switch (addrtype) {
        case AF_INET:
            memcpy(&p4->sin_addr, addr, sizeof(struct in_addr));
            p4->sin_port = htons(port);
            a_len = sizeof (struct sockaddr_in);
            break;
        case AF_INET6:
            memcpy(&p6->sin6_addr, addr, sizeof(struct in6_addr));
            p6->sin6_port = htons(port);
            a_len = sizeof (struct sockaddr_in6);
            break;
        default:
            return update_error(glb_cc, EINVAL, POSIX_ERROR,
			    "Unsupported address type (%d)", addrtype);
            break;
    }
    
    sock = socket(a.ss_family, SOCK_STREAM, 0);
    if (sock == -1)
        return update_error(glb_cc, errno, POSIX_ERROR,
                "Failed to create network socket");

/* TODO from L&B; do we need it?
    opt = 1;
    setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,&opt,sizeof opt);
*/

    if (timeout) {
        int flags = fcntl(sock, F_GETFL, 0);
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
            return update_error(glb_cc, errno, POSIX_ERROR,
                    "Failed to set socket file status flags");
        gettimeofday(&before,NULL);
    }

    if (connect(sock,(struct sockaddr *) &a, a_len) < 0) {
        if (timeout && errno == EINPROGRESS) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            /*TODO why don't we use timeout explicitly?*/
            memcpy(&to, timeout, sizeof to);
            gettimeofday(&before, NULL);
            switch (select(sock+1, NULL, &fds, NULL, &to)) {
                case -1: close(sock);
                         return update_error(glb_cc, errno, POSIX_ERROR,
                                 "Connection error");
                case 0: close(sock);
                        timeout->tv_sec = 0; 
                        timeout->tv_usec = 0; 
                        return update_error(glb_cc, errno, POSIX_ERROR,
                                "Connection timeout reached");
            }
            gettimeofday(&after, NULL);
            tv_sub(after, before);
            tv_sub(*timeout, after);

            err_len = sizeof sock_err;
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &sock_err, &err_len)) {
                close(sock);
                return update_error(glb_cc, errno, POSIX_ERROR,
                        "Cannost get socket options");
            }
            if (sock_err) {
                close(sock);
                errno = sock_err;
                return update_error(glb_cc, errno, POSIX_ERROR,
                        "Connection error");
            }
        }
        else {
            close(sock);
            return update_error(glb_cc, errno, POSIX_ERROR,
                    "Connection error");
        }
    }

    io_cc->sock = sock;
    return 0;
}

/*TODO select + timeout, EINTR!!! */ 
canl_err_code
canl_io_accept(canl_ctx cc, canl_io_handler io, int new_fd,
        struct sockaddr s_addr, int flags, canl_principal *peer,
        struct timeval *timeout)
{
    int err = 0;
    io_handler *io_cc = (io_handler*) io;
    glb_ctx *glb_cc = (glb_ctx*) cc;
    struct canl_mech *mech = find_mech(GSS_C_NO_OID);
    void *conn_ctx = NULL;

    if (!glb_cc) 
        return EINVAL; /* XXX Should rather be a CANL error */

    if (!io_cc)
        return set_error(cc, EINVAL, POSIX_ERROR, "IO handler not initialized");

    io_cc->sock = new_fd;

    err = mech->server_init(glb_cc, &conn_ctx);
    if (err)
        goto end;

    err = mech->accept(glb_cc, io_cc, conn_ctx, timeout); 
    if (err)
	goto end;

    /* If peer != NULL then client certificate is mandatory*/
      if (peer) {
	err = mech->get_peer(glb_cc, io_cc, conn_ctx, peer);
	if (err)
	    goto end;
    }

    io_cc->conn_ctx = conn_ctx;
    io_cc->oid = GSS_C_NO_OID;

    err = 0;

end:
    if (err) {
        (io_cc)->sock = -1;
        if (conn_ctx)
            mech->finish(glb_cc, conn_ctx);
    }

    return err;
}

/* close connection, preserve some info for the future reuse */
canl_err_code
canl_io_close(canl_ctx cc, canl_io_handler io)
{
    io_handler *io_cc = (io_handler*) io;
    glb_ctx *glb_cc = (glb_ctx*) cc;
    int err = 0;
    canl_mech *mech;

    /*check cc and io*/
    if (!cc) {
        return EINVAL; /* XXX Should rather be a CANL error */
    }

    if (!io)
	return set_error(cc, EINVAL, POSIX_ERROR, "IO handler not initialized");

    if (io_cc->conn_ctx) {
	mech = find_mech(io_cc->oid);
	mech->close(glb_cc, io, io_cc->conn_ctx);
	/* XXX can it be safely reopened ?*/
    }

    if (io_cc->sock != -1) {
        close (io_cc->sock);
        io_cc->sock = -1;
    }

    return err;
}

static void io_destroy(glb_ctx *cc, io_handler *io)
{
    io_handler *io_cc = (io_handler*) io;
    canl_mech *mech;
    
    if (io == NULL)
	return;

    if (io_cc->conn_ctx) {
	mech = find_mech(io->oid);
	mech->finish(cc, io_cc->conn_ctx);
	io_cc->conn_ctx = NULL;
	io_cc->oid = GSS_C_NO_OID;
    }

    return;
}


canl_err_code
canl_io_destroy(canl_ctx cc, canl_io_handler io)
{
    int err = 0;
    glb_ctx *glb_cc = (glb_ctx*) cc;
    io_handler *io_cc = (io_handler*) io;
    /*check cc and io*/

    if (!glb_cc) {
        return EINVAL; /* XXX Should rather be a CANL error */
    }

    if (!io_cc)
	return set_error(glb_cc, EINVAL, POSIX_ERROR,  "Invalid io handler");

    canl_io_close(cc, io);

    io_destroy(glb_cc, io_cc);
    free (io_cc);

    return err;
}

/* XXX: 0 returned returned by ssl_read() means error or EOF ? */
size_t canl_io_read(canl_ctx cc, canl_io_handler io, void *buffer, size_t size, struct timeval *timeout)
{
    io_handler *io_cc = (io_handler*) io;
    glb_ctx *glb_cc = (glb_ctx*) cc;
    int b_recvd = 0;
    struct canl_mech *mech;
    
    if (!cc)
        return -1;

    if (!io) {
	 set_error(cc, EINVAL, POSIX_ERROR, "IO handler not initialized");
	 return -1;
    }

    if (io_cc->conn_ctx == NULL)
	return set_error(cc, EINVAL, POSIX_ERROR, "Connection not secured");
    
    if (!buffer || !size) {
	set_error(cc, EINVAL, POSIX_ERROR, "No memory to write into");
	return -1;
    }

    mech = find_mech(io_cc->oid);

    b_recvd = mech->read(glb_cc, io_cc, io_cc->conn_ctx,
		         buffer, size, timeout);

    return b_recvd;
}

size_t canl_io_write(canl_ctx cc, canl_io_handler io, void *buffer, size_t size, struct timeval *timeout)
{
    io_handler *io_cc = (io_handler*) io;
    glb_ctx *glb_cc = (glb_ctx*) cc;
    int b_written = 0;
    struct canl_mech *mech;

    if (!cc)
        return -1;

    if (!io) {
	set_error(cc, EINVAL, POSIX_ERROR, "IO handler not initialized");
	return -1;
    }

    if (io_cc->conn_ctx == NULL)
	return set_error(cc, EINVAL, POSIX_ERROR, "Connection not secured");

    if (!buffer || !size) {
	set_error(cc, EINVAL, POSIX_ERROR, "No memory to read from");
	return -1;
    }

    mech = find_mech(io_cc->oid);

    b_written = mech->write(glb_cc, io_cc, io_cc->conn_ctx,
			    buffer, size, timeout);

    return b_written;
}

#if 0
int canl_set_ctx_own_cert(canl_ctx cc, canl_x509 cert, 
        canl_stack_of_x509 chain, canl_pkey key)
{
    glb_ctx *glb_cc = (glb_ctx*) cc;
    int err = 0;

    if (!cc)
        return EINVAL;
    if(!cert)
        return set_error(glb_cc, EINVAL, POSIX_ERROR, "invalid"
                "parameter value");

    err = do_set_ctx_own_cert(glb_cc, cert, chain, key);
    if(err) {
        update_error(glb_cc, "can't set cert or key to context");
    }
        return err;
}

//TODO callback and userdata process
int canl_set_ctx_own_cert_file(canl_ctx cc, char *cert, char *key,
        canl_password_callback cb, void *userdata)
{
    glb_ctx *glb_cc = (glb_ctx*) cc;
    int err = 0;

    if (!cc)
        return EINVAL;
    if(!cert ) {
        set_error(glb_cc, EINVAL, POSIX_ERROR, "invalid parameter value");
        return EINVAL;
    }

    err = do_set_ctx_own_cert_file(glb_cc, cert, key);
    if(err) {
        update_error(glb_cc, "can't set cert or key to context");
    }
        return err;
}
#endif

struct canl_mech *
find_mech(gss_OID oid)
{
    /* XXX */
    return &canl_mech_ssl;
}

canl_err_code
canl_princ_name(canl_ctx cc, const canl_principal princ, char **name)
{
    struct _principal_int *p = (struct _principal_int *) princ;

    if (cc == NULL)
	return EINVAL;
    if (princ == NULL)
	return set_error(cc, EINVAL, POSIX_ERROR, "Principal not initialized");

    if (name == NULL)
	return set_error(cc, EINVAL, POSIX_ERROR, "invalid parameter value");

    *name = strdup(p->name);
    if (*name == NULL)
	return set_error(cc, ENOMEM, POSIX_ERROR, "not enough memory");

    return 0;
}

void
canl_princ_free(canl_ctx cc, canl_principal princ)
{
    struct _principal_int *p = (struct _principal_int *) princ;

    if (cc == NULL)
	return;
    if (princ == NULL)
	return;

    if (p->name)
	free(p->name);
    free(princ);

    return;
}

canl_err_code CANL_CALLCONV
canl_ctx_set_pkcs11_lib(canl_ctx ctx, const char *lib)
{
    setenv("PKCS11_LIB", lib, 1);
    return 0;
}

canl_err_code CANL_CALLCONV
canl_ctx_set_pkcs11_init_args(canl_ctx ctx, const char *args)
{
    setenv("PKCS11_INIT_ARGS", args, 1);
    return 0;
}
