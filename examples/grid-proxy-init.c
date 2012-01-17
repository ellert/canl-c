#include <canl.h>
#include <canl_cred.h>

int
main(int argc, char *argv[])
{
    canl_cred signer = NULL;
    canl_cred proxy = NULL;
    canl_x509_req proxy_req = NULL;
    canl_ctx ctx = NULL;
    canl_err_code ret;

    ctx = canl_create_ctx();
    if (ctx == NULL) {
	fprintf(stderr, "Failed to create library context\n");
	return 1;
    }

/* First create a certificate request with a brand-new keypair */
    ret = canl_req_create(ctx, &proxy_req);
    if (ret) {
	fprintf(stderr, "Failed to create certificate request container: %s\n",
		canl_get_error_message(ctx));
	return 1;
    }

    ret = canl_req_gen_key(ctx, proxy_req, 1024);
    if (ret) {
	fprintf(stderr, "Failed to generate key-pair: %s\n",
		canl_get_error_message(ctx));
	ret = 1;
	goto end;
    }

/* Create a new structure for the proxy certificate to be signed copying the key-pairs just created */
    ret = canl_cred_create(ctx, &proxy);
    ret = canl_cred_load_req(ctx, proxy, proxy_req);
    ret = canl_cred_set_lifetime(ctx, proxy, 60*10);
    ret = canl_cred_set_cert_type(ctx, proxy, CANL_RFC);

/* Load the signing credentials */
    ret = canl_cred_create(ctx, &signer);
    ret = canl_cred_load_cert_file(ctx, signer, "$HOME/.globus/usercert.pem");
    ret = canl_cred_load_priv_key_file(ctx, signer, "$HOME/.globus/userkey.pem", NULL, NULL);
    /* export lookup routines ?? */

#ifdef VOMS
    GET_VOMS_EXTS(ctx, signer, STACK_OF(EXTS));
    foreach (EXTS)
	ret = canl_cred_set_ext(ctx, proxy, ext);
#endif

/* Create the proxy certificate */
    ret = canl_cred_sign_proxy(ctx, signer, proxy);

/* and store it in a file */
    ret = canl_cred_save_proxyfile(ctx, proxy, "/tmp/x509up_u11930");

    ret = 0;

end:
    if (signer)
	canl_cred_free(ctx, signer);
    if (proxy)
	canl_cred_free(ctx, proxy);
    if (proxy_req)
	canl_req_free(ctx, proxy_req);
    if (ctx)
	canl_free_ctx(ctx);

    return ret;
}