#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <canl.h>
#include <canl_ssl.h>

#define BUF_LEN 1000
#define DEF_PORT 4321
#define DEF_TIMEOUT 150

int main(int argc, char *argv[])
{
    canl_ctx my_ctx;
    canl_io_handler my_io_h = NULL;
    int err = 0;
    char buf[BUF_LEN];
    int buf_len = 0;
    char *ca_dir = NULL;
    char *p_server = NULL;
    char *def_server = "www.linuxfoundation.org";
    int opt, port = DEF_PORT;
    struct timeval timeout;
    char *serv_cert = NULL;
    char *serv_key = NULL;
    char *proxy_cert = NULL;
    canl_principal princ = NULL;
    int get_peer_princ = 0;
    char *name = NULL;
    int ocsp_on = 0;

    timeout.tv_sec = DEF_TIMEOUT;
    timeout.tv_usec = 0;

    while ((opt = getopt(argc, argv, "nhop:s:c:k:t:x:")) != -1) {
        switch (opt) {
            case 'h':
                fprintf(stderr, "Usage: %s [-p port] [-c certificate]"
                        " [-k private key] [-d ca_dir] [-h] "
                        " [-s server] [-x proxy certificate] "
                        "[-t timeout] [-n {print peer's princ name}] "
                        "[-o {turn OCSP on}] "
                        " \n", argv[0]);
                exit(0);
            case 'p':
                port = atoi(optarg);
                break;
            case 's':
                p_server = optarg;
                break;
            case 'c':
                serv_cert = optarg;
                break;
            case 'k':
                serv_key = optarg;
                break;
            case 'x': 
                proxy_cert = optarg;
                break;
            case 'd':
                ca_dir = optarg;
                break;
            case 't':
                timeout.tv_sec = atoi(optarg);
                break;
            case 'n':
                get_peer_princ = 1;
                break;
            case 'o':
                ocsp_on = 1;
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s [-p port] [-c certificate]"
                        " [-k private key] [-d ca_dir] [-h]"
                        " [-s server] [-x proxy certificate]"
                        "[-t timeout] [-n {print peer's princ name}] "
                        "[-o {turn OCSP on}] "
                        " \n", argv[0]);
                exit(-1);
        }
    }

    if (!p_server)
        p_server = def_server;

    my_ctx = canl_create_ctx();
    if (!my_ctx){
	printf("CANL context cannot be created, exiting.\n");
        goto end;
    }

    err = canl_create_io_handler(my_ctx, &my_io_h);
    if (err) {
        printf("io handler cannot be created:\n[CANL] %s\n",
                canl_get_error_message(my_ctx));
        goto end;
    }
    
    if (serv_cert || serv_key || proxy_cert){
        err = canl_ctx_set_ssl_cred(my_ctx, serv_cert, serv_key, proxy_cert,
                                     NULL, NULL);
        if (err) {
            printf("[CLIENT] cannot set certificate or key" 
                   " to context:\n[CANL] %s\n",
                    canl_get_error_message(my_ctx));
            goto end;
        }
    }
    if (ocsp_on)
        canl_ctx_set_ssl_flags(my_ctx, CANL_SSL_OCSP_VERIFY_ALL);

     if (get_peer_princ) {
        err = canl_io_connect(my_ctx, my_io_h, p_server, NULL, port, NULL, 0,
            &princ, &timeout);
        if (err) {
            printf("[CLIENT] connection cannot be established:\n[CANL] %s\n",
                    canl_get_error_message(my_ctx));
            goto end;
        }

        err = canl_princ_name(my_ctx, princ, &name);
        printf("[CLIENT] connection established with %s\n", name);
        free(name);
        canl_princ_free(my_ctx, princ);
    }
    else{
        err = canl_io_connect(my_ctx, my_io_h, p_server, NULL, port, NULL, 0,
            NULL, &timeout);
        if (err) {
            printf("[CLIENT] connection cannot be established:\n[CANL] %s\n",
                    canl_get_error_message(my_ctx));
            goto end;
        }
        printf("[CLIENT] connection established\n");
    }

    strcpy(buf, "This is the testing message to send");
    buf_len = strlen(buf) + 1;

    printf("[CLIENT] Trying to send sth to the server\n");
    err = canl_io_write (my_ctx, my_io_h, buf, buf_len, &timeout);
    if (err <= 0) {
        printf("can't write using ssl:\n[CANL] %s\n",
	       canl_get_error_message(my_ctx));
        goto end;
    }
    else {
        buf[err] = '\0';
        printf("[CLIENT] message \"%s\" sent successfully\n", buf);
    }
    buf[0] = '\0';

    err = canl_io_read (my_ctx, my_io_h, buf, sizeof(buf)-1, &timeout);
    if (err > 0) {
        buf[err] = '\0';
        printf ("[CLIENT] received: %s\n", buf);
        err = 0;
    }

end:
    if (my_io_h)
	canl_io_destroy(my_ctx, my_io_h);

    canl_free_ctx(my_ctx);

    return err;
}
