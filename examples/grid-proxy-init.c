#include <stdio.h>
#include <unistd.h>
#include <canl.h>
#include <canl_cred.h>

#define BITS 1024
#define LIFETIME 43200 /*12 hours*/
#define OUTPUT "/tmp/x509_u99999" 

int main(int argc, char *argv[])
{
    canl_cred signer = NULL;
    canl_cred proxy = NULL;
    canl_ctx ctx = NULL;
    canl_err_code ret = 0;
    char *user_cert = NULL;
    char *output = NULL;
    char *user_key = NULL;
    long int lifetime = 0;
    unsigned int bits = 0;
    int opt = 0;
    int proxyver = 2;
    enum canl_cert_type cert_type = CANL_RFC;

    while ((opt = getopt(argc, argv, "hc:k:l:b:o:v:")) != -1) {
        switch (opt) {
            case 'h':
                fprintf(stderr, "Usage: %s [-c certificate]"
                        " [-k private key] [-h] [-l lifetime] [-b bits]"
                        " [-o output] [-v proxy version]"
                       "\n", argv[0]);
                exit(0);
            case 'c':
                user_cert = optarg;
                break;
            case 'k':
                user_key = optarg;
                break;
            case 'l':
		lifetime = atoi(optarg);
                break;
            case 'b':
                bits = atoi(optarg);
                break;
            case 'o':
                output = optarg;
                break;
            case 'v':
                proxyver = atoi(optarg);
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s [-c certificate]"
                        " [-k private key] [-h] [-l lifetime] [-b bits]"
                        " [-o output] [-v proxy version]"
                       "\n", argv[0]);
                exit(-1);
        }
    }
    switch (proxyver){
        case 2:
           cert_type = CANL_EEC;
           break;
        case 3:
           cert_type = CANL_RFC;
           break;
        default:
           cert_type = CANL_RFC;
    }

    ctx = canl_create_ctx();
    if (ctx == NULL) {
	fprintf(stderr, "[PROXY-INIT] Failed to create library context\n");
	return 1;
    }

    /* First create a certificate request with a brand-new keypair */
    ret = canl_cred_new(ctx, &proxy);
    if (ret){
        fprintf(stderr, "[PROXY-INIT] Proxy context cannot be created"
                ": %s\n", canl_get_error_message(ctx));
        goto end;
    }

    if (!bits)
        bits = BITS;
    ret = canl_cred_new_req(ctx, proxy, bits);
    if (ret) {
	fprintf(stderr, "[PROXY-INIT] Failed to create certificate "
                "request container: %s\n", canl_get_error_message(ctx));
	goto end;
    }

    if (!lifetime)
        lifetime = LIFETIME;
    /*Create key-pairs implicitly*/
    ret = canl_cred_set_lifetime(ctx, proxy, lifetime);
    if (ret)
	fprintf(stderr, "[PROXY-INIT] Failed set new cert lifetime"
                ": %s\n", canl_get_error_message(ctx));
    
    ret = canl_cred_set_cert_type(ctx, proxy, cert_type);
    if (ret)
	fprintf(stderr, "[PROXY-INIT] Failed set new cert type"
                ": %s\n", canl_get_error_message(ctx));
    
    /* Load the signing credentials */
    ret = canl_cred_new(ctx, &signer);
    if (ret){
        fprintf(stderr, "[PROXY-INIT] Proxy context cannot be created"
                ": %s\n", canl_get_error_message(ctx));
        goto end;
    }
    
    ret = canl_cred_load_cert_file(ctx, signer, user_cert);
    if (ret){
        fprintf(stderr, "[PROXY-INIT] Cannot load signer's certificate"
                ": %s\n", canl_get_error_message(ctx));
        goto end;
    }
    ret = canl_cred_load_priv_key_file(ctx, signer, user_key, NULL, NULL);
    if (ret){
        fprintf(stderr, "[PROXY-INIT] Cannot access signer's key"
                ": %s\n", canl_get_error_message(ctx));
        goto end;
    }
    /*TODO? export lookup routines ?? */

#ifdef VOMS
    GET_VOMS_EXTS(ctx, signer, STACK_OF(EXTS));
    foreach (EXTS) {
        ret = canl_cred_set_extension(ctx, proxy, ext);
        if (ret){
            fprintf(stderr, "[PROXY-INIT] Cannot set voms extension"
                    ": %s\n", canl_get_error_message(ctx));
        }
    }
#endif

/* Create the proxy certificate */
    ret = canl_cred_sign_proxy(ctx, signer, proxy);
    if (ret){
        fprintf(stderr, "[PROXY-INIT] Cannot sign new proxy"
                ": %s\n", canl_get_error_message(ctx));
        goto end;
    }

/* and store it in a file */
    if (!output)
        output = OUTPUT;
    ret = canl_cred_save_proxyfile(ctx, proxy, output);
    if (ret){
        fprintf(stderr, "[PROXY-INIT] Cannot save new proxy"
                ": %s\n", canl_get_error_message(ctx));
        goto end;
    }

    ret = 0;

end:
    if (signer)
	canl_cred_free(ctx, signer);
    if (proxy)
	canl_cred_free(ctx, proxy);
    if (ctx)
	canl_free_ctx(ctx);

    return ret;
}
