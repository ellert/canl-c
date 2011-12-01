#include "canl_locl.h"

static int do_ssl_connect( glb_ctx *cc, io_handler *io, struct timeval *timeout);
static int do_ssl_accept( glb_ctx *cc, io_handler *io, struct timeval *timeout);

int ssl_init(glb_ctx *cc, io_handler *io)
{
    int err = 0;
    CANL_ERROR_ORIGIN e_orig = unknown_error;

    if (!cc) {
        return EINVAL;
    }
    if (!io) {
        err = EINVAL;
        e_orig = posix_error;
        goto end;
    }

    SSL_load_error_strings();
    SSL_library_init();

    io->s_ctx->ssl_meth = SSLv23_method();  //TODO dynamically
    io->s_ctx->ssl_ctx = SSL_CTX_new(io->s_ctx->ssl_meth);
    if (!io->s_ctx->ssl_ctx){
        err = 1; //TODO set appropriate
            goto end;
    }

end:
    if (err)
        set_error(cc, err, e_orig, "cannot initialize SSL context (ssl_init)");
    return err;

}

int ssl_connect(glb_ctx *cc, io_handler *io, struct timeval *timeout)
{
    int err = 0, flags;

    if (!cc) {
        return EINVAL;
    }
    if (!io) {
        err = EINVAL;
        goto end;
    }

    flags = fcntl(io->sock, F_GETFL, 0);
    (void)fcntl(io->sock, F_SETFL, flags | O_NONBLOCK);

    io->s_ctx->bio_conn = BIO_new_socket(io->sock, BIO_NOCLOSE);
    (void)BIO_set_nbio(io->s_ctx->bio_conn,1);

    io->s_ctx->ssl_io = SSL_new(io->s_ctx->ssl_ctx);
    //setup_SSL_proxy_handler(io->s_ctx->ssl_ctx, cacertdir);
    SSL_set_bio(io->s_ctx->ssl_io, io->s_ctx->bio_conn, io->s_ctx->bio_conn);

    io->s_ctx->bio_conn = NULL; //TODO WHAT THE HELL IS THIS???? 

    if ((err = do_ssl_connect(cc, io, timeout))) {
        goto end;
    }

    /*
       if (post_connection_check(io->s_ctx->ssl_io)) {
       opened = 1;
       (void)Send("0");
       return 1;
       }
     */

end:
    return err;
}

int ssl_accept(glb_ctx *cc, io_handler *io, io_handler *new_io, 
        struct timeval *timeout)
{
    int err = 0, flags;

    if (!cc) {
        return EINVAL;
    }
    if (!io) {
        err = EINVAL;
        goto end;
    }

    flags = fcntl(new_io->sock, F_GETFL, 0);
    (void)fcntl(new_io->sock, F_SETFL, flags | O_NONBLOCK);

    new_io->s_ctx->bio_conn = BIO_new_socket(new_io->sock, BIO_NOCLOSE);
    (void)BIO_set_nbio(new_io->s_ctx->bio_conn,1);

    new_io->s_ctx->ssl_io = SSL_new(new_io->s_ctx->ssl_ctx);
    //setup_SSL_proxy_handler(io->s_ctx->ssl_ctx, cacertdir);
    SSL_set_bio(new_io->s_ctx->ssl_io, new_io->s_ctx->bio_conn, 
            new_io->s_ctx->bio_conn);

    if ((err = do_ssl_accept(cc, new_io, timeout))) {
        goto end;
    }

    /*
       if (post_connection_check(io->s_ctx->ssl_io)) {
       opened = 1;
       (void)Send("0");
       return 1;
       }
     */

end:
    return err;
}

/*
 * Encapsulates select behaviour
 *
 * Returns:
 *     > 0 : Ready to read or write.
 *     = 0 : timeout reached.
 *     < 0 : error.
 */
int do_select(int fd, time_t starttime, int timeout, int wanted)
{
    fd_set rset;
    fd_set wset;

    FD_ZERO(&rset);
    FD_ZERO(&wset);

    if (wanted == 0 || wanted == SSL_ERROR_WANT_READ)
        FD_SET(fd, &rset);
    if (wanted == 0 || wanted == SSL_ERROR_WANT_WRITE)
        FD_SET(fd, &wset);

    int ret = 0;

    if (timeout != -1) {
        struct timeval endtime;

        time_t curtime = time(NULL);

        if (curtime - starttime >= timeout)
            return 0;

        endtime.tv_sec = timeout - (curtime - starttime);
        endtime.tv_usec = 0;

        ret = select(fd+1, &rset, &wset, NULL, &endtime);
    }
    else {
        ret = select(fd+1, &rset, &wset, NULL, NULL);
    }

    if (ret == 0)
        return 0;

    if ((wanted == SSL_ERROR_WANT_READ && !FD_ISSET(fd, &rset)) ||
            (wanted == SSL_ERROR_WANT_WRITE && !FD_ISSET(fd, &wset)))
        return -1;

    if (ret < 0 && (!FD_ISSET(fd, &rset) || !FD_ISSET(fd, &wset)))
        return 1;

    return ret;
}

#define TEST_SELECT(ret, ret2, timeout, curtime, starttime, errorcode) \
    ((ret) > 0 && ((ret2) <= 0 && (((timeout) == -1) ||                  \
            (((timeout) != -1) &&                 \
             ((curtime) - (starttime)) < (timeout))) && \
        ((errorcode) == SSL_ERROR_WANT_READ ||                 \
         (errorcode) == SSL_ERROR_WANT_WRITE)))

static int do_ssl_connect( glb_ctx *cc, io_handler *io, struct timeval *timeout)
{
    time_t starttime, curtime;
    int ret = -1, ret2 = -1, err = 0;
    long errorcode = 0;
    int expected = 0;
    int locl_timeout = -1;

    /* do not take tv_usec into account in this function*/
    if (timeout)
        locl_timeout = timeout->tv_sec;
    else
        locl_timeout = -1;
    curtime = starttime = time(NULL);

    do {
        ret = do_select(io->sock, starttime, locl_timeout, expected);
        if (ret > 0) {
            ret2 = SSL_connect(io->s_ctx->ssl_io);
            expected = errorcode = SSL_get_error(io->s_ctx->ssl_io, ret2);
        }
        curtime = time(NULL);
    } while (TEST_SELECT(ret, ret2, locl_timeout, curtime, starttime, errorcode));

    //TODO split ret2 and ret into 2 ifs to set approp. error message
    if (ret2 <= 0 || ret <= 0) {
        if (timeout && (curtime - starttime >= locl_timeout)){
            timeout->tv_sec=0;
            timeout->tv_usec=0;
            err = ETIMEDOUT; 
            set_error (cc, err, posix_error, "Connection stuck during handshake: timeout reached (do_ssl_connect)");
        }
        else{
            err = -1; //TODO set approp. error message
            set_error (cc, err, unknown_error,"Error during SSL handshake (do_ssl_connect)");
        }
        return err;
    }

    return 0;
}

static int do_ssl_accept( glb_ctx *cc, io_handler *io, struct timeval *timeout)
{
    time_t starttime, curtime;
    int ret = -1, ret2 = -1, err = 0;
    long errorcode = 0;
    int expected = 0;
    int locl_timeout = -1;

    /* do not take tv_usec into account in this function*/
    if (timeout)
        locl_timeout = timeout->tv_sec;
    else
        locl_timeout = -1;
    curtime = starttime = time(NULL);

    do {
        ret = do_select(io->sock, starttime, locl_timeout, expected);
        if (ret > 0) {
            ret2 = SSL_accept(io->s_ctx->ssl_io);
            expected = errorcode = SSL_get_error(io->s_ctx->ssl_io, ret2);
        }
        curtime = time(NULL);
    } while (ret > 0 && (ret2 <= 0 && ((locl_timeout == -1) ||
           ((locl_timeout != -1) &&
            (curtime - starttime) < locl_timeout)) &&
           (errorcode == SSL_ERROR_WANT_READ ||
            errorcode == SSL_ERROR_WANT_WRITE)));

    //TODO split ret2 and ret into 2 ifs to set approp. error message
    if (ret2 <= 0 || ret <= 0) {
        if (timeout && (curtime - starttime >= locl_timeout)){
            timeout->tv_sec=0;
            timeout->tv_usec=0;
            err = ETIMEDOUT; 
            set_error (cc, err, posix_error, "Connection stuck during handshake: timeout reached (do_ssl_accept)");
        }
        else{
            err = -1; //TODO set approp. error message
            set_error (cc, err, unknown_error, "Error during SSL handshake (do_ssl_accept)");
        }
        return err;
    }

    return 0;
}

/* this function has to return # bytes written or ret < 0 when sth went wrong*/
int ssl_write(glb_ctx *cc, io_handler *io, void *buffer, size_t size, struct timeval *timeout)
{
    int err = 0;
    int ret = 0, nwritten=0;
    const char *str;
    int fd = -1; 
    time_t starttime, curtime;
    int do_continue = 0;
    int expected = 0;
    int locl_timeout;
    int touted = 0;
    int to = 0; // bool

    if (!io->s_ctx->ssl_io) {
        err = EINVAL;
        goto end;
    }

    if (!cc) {
        return -1;
    }
    if (!io) {
        err = EINVAL;
        goto end;
    }

    if (!buffer) {
        err = EINVAL; //TODO really?
        set_error(cc, err, posix_error, "Nothing to write (ssl_write)");
        errno = err;
        return -1;
    }
    
    fd = BIO_get_fd(SSL_get_rbio(io->s_ctx->ssl_io), NULL);
    str = buffer;//TODO !!!!!! text.c_str();

    curtime = starttime = time(NULL);
    if (timeout) {
        locl_timeout = timeout->tv_sec;
        to = 1;
    }
    else {
        to = 0;
        locl_timeout = -1;
    }

    do {
        ret = do_select(fd, starttime, locl_timeout, expected);

        do_continue = 0;
        if (ret > 0) {
            int v;
            errno = 0;
            ret = SSL_write(io->s_ctx->ssl_io, str + nwritten, strlen(str) - nwritten);
            v = SSL_get_error(io->s_ctx->ssl_io, ret);

            switch (v) {
                case SSL_ERROR_NONE:
                    nwritten += ret;
                    if ((size_t)nwritten == strlen(str))
                        do_continue = 0;
                    else
                        do_continue = 1;
                    break;

                case SSL_ERROR_WANT_READ:
                case SSL_ERROR_WANT_WRITE:
                    expected = v;
                    ret = 1;
                    do_continue = 1;
                    break;

                default:
                    do_continue = 0;
            }
        }
        curtime = time(NULL);
        if (to)
            locl_timeout = locl_timeout - (curtime - starttime);
        if (to && locl_timeout <= 0){
            touted = 1;
            goto end;
        }
    } while (ret <= 0 && do_continue);

end:
    if (err) {
        errno = err;
        set_error (cc, err, posix_error, "Error during SSL write (ssl_write)");
        return -1;
    }
    if (touted){
       errno = err = ETIMEDOUT;
       set_error(cc, err, posix_error, "Connection stuck during write: timeout reached (ssl_write)");
       return -1;
    }
    if (ret <=0){
        err = -1;//TODO what to assign??????
        set_error (cc, err, unknown_error, "Error during SSL write (ssl_write)");
    }
    return ret;
}

int ssl_read(glb_ctx *cc, io_handler *io, void *buffer, size_t size, struct timeval *tout)
{
    int err = 0;
    int ret = 0, nwritten=0, ret2 = 0;
    char *str;
    int fd = -1;
    time_t starttime, curtime;
    int expected = 0, error = 0;
    int timeout;

    if (!io->s_ctx->ssl_io) {
        err = EINVAL;
        goto end;
    }

    if (!cc) {
        return -1;
    }
    if (!io) {
        err = EINVAL;
        goto end;
    }

    if (!buffer) {
        err = EINVAL; //TODO really?
        set_error(cc, err, posix_error, "Not enough memory to read to (ssl_read)");
        errno = err;
        return -1;
    }

    fd = BIO_get_fd(SSL_get_rbio(io->s_ctx->ssl_io), NULL);
    str = buffer;//TODO !!!!!! text.c_str();

    curtime = starttime = time(NULL);
    if (tout) {
        timeout = tout->tv_sec;
    }
    else
        timeout = -1;
    do {
        ret = do_select(fd, starttime, timeout, expected);
        curtime = time(NULL);

        if (ret > 0) {
            ret2 = SSL_read(io->s_ctx->ssl_io, str + nwritten, strlen(str) - nwritten);

            if (ret2 <= 0) {
                expected = error = SSL_get_error(io->s_ctx->ssl_io, ret2);
            }
        }
    } while (TEST_SELECT(ret, ret2, timeout, curtime, starttime, error));

end:
    if (ret <= 0 || ret2 <= 0) { //TODO ret2 < 0 originally
        err = -1; //TODO what to assign
        if (timeout != -1 && (curtime - starttime >= timeout)){
            set_error(cc, ETIMEDOUT, posix_error, "Connection stuck during read: timeout reached. (ssl_read)");
        }
        else
            set_error(cc, err, unknown_error, "Error during SSL read: (ssl_read)");
    }
    else
        err = ret2;
    return err;
}