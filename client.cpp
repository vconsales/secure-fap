#include "commonlib/net_wrapper.h"
#include "commonlib/messages.h"
#include "commonlib/commonlib.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/err.h>


uint64_t cl_nonce;
uint64_t sr_nonce;

uint64_t generate_nonce()
{
	uint64_t nonce;
	RAND_bytes((unsigned char*)&nonce,8);
	return nonce;
}

bool read_pub_key(const char *filename, EVP_PKEY** pubkeys)
{
        //EVP_PKEY* pubkeys[1];
        FILE* file = fopen(filename, "r");

        if(file == NULL)
                return false;

        pubkeys[0] = PEM_read_PUBKEY(file, NULL, NULL, NULL);
        if(pubkeys[0] == NULL)
                return false;

        fclose(file);
        return true;
}

unsigned int readcontent(const char *filename, unsigned char** fcontent)
{
	unsigned int fsize = 0;
	FILE *fp;

	fp = fopen(filename, "r");
	if(fp) {
		fseek(fp, 0, SEEK_END);
		fsize = ftell(fp);
		rewind(fp);

		//printf("fsize is %u \n",fsize);
		*fcontent = new unsigned char[fsize + 1];
		fread(*fcontent, 1, fsize, fp);
		(*fcontent)[fsize] = '\0';

		fclose(fp);
	} else {
		perror("file doesn't exist \n");
		return 0;
	}
	return fsize + 1;
}

int send_hello_msg(int sock) {
	hello_msg h;
	h.t = CLIENT_HELLO;
	h.nonce = cl_nonce = generate_nonce();
	convert_to_network_order(&h);
	printf("client sends nonce: %ld\n",cl_nonce);
	return send_data(sock,(unsigned char*)&h, sizeof(h));
}

int analyze_message(unsigned char* buf)
{
	convert_to_host_order(buf);
	switch( ((simple_msg*)buf)->t ) {
  		case SERVER_HELLO:
  			sr_nonce = ((hello_msg*)buf)->nonce;
  			printf("Server nonce received: %ld\n",sr_nonce);
  			break;
		default:
			return -2;
	}

	return 0;
}

int main(int argc, char **argv) 
{
	int sd;
	unsigned char *buffer_file = NULL;
	unsigned int file_len = 0;
	uint16_t server_port;

	unsigned char *ciphertext;
	EVP_CIPHER_CTX *ctx;
	int outlen=0, cipherlen = 0;
	unsigned char *iv;

	unsigned char* encrypted_keys[1];
	int encrypted_keys_len[1];
	EVP_PKEY* pubkeys[1];
	int evp_res;

	my_buffer my_buff;
	my_buff.buf = NULL;
	my_buff.size = 0;

	if( argc < 3 ){
		perror("use: ./client filename server_ip port");
		return -1;
	}
	sscanf(argv[3],"%hd",&server_port);

	sd = start_tcp_connection(argv[2], server_port);
	if( sd < 0 )
		return -1;
	send_hello_msg(sd);
	recv_data(sd,&my_buff);
	analyze_message(my_buff.buf);

	// leggo l contenuto del file da inviare
	file_len = readcontent(argv[1],&buffer_file);

	if( !read_pub_key("keys/rsa_server_pubkey.pem",pubkeys) ){
		printf("Cannot read public key file\n");
		return -1;
	}

	encrypted_keys_len[0] = EVP_PKEY_size(pubkeys[0]);
	encrypted_keys[0] = new unsigned char[encrypted_keys_len[0]];
	ciphertext = new unsigned char[file_len + 16];

	ctx = new EVP_CIPHER_CTX;
	iv = new unsigned char[EVP_CIPHER_iv_length(EVP_aes_128_cbc())];
	if( iv == NULL ){
		printf("Cannot allocate iv \n");
		return -1;
	}
	evp_res = EVP_SealInit(ctx, EVP_aes_128_cbc(), encrypted_keys, encrypted_keys_len, iv, pubkeys, 1);
	if(evp_res == 0)
		printf("EVP_SealInit Error: %s\n", ERR_error_string(ERR_get_error(), NULL));

	evp_res = EVP_SealUpdate(ctx, ciphertext, &outlen, (unsigned char*)buffer_file, file_len);
	if(evp_res == 0)
		printf("EVP_SealUpdate Error: %s\n", ERR_error_string(ERR_get_error(), NULL));

	cipherlen = outlen;
	evp_res = EVP_SealFinal(ctx, ciphertext+cipherlen, &outlen);
	if(evp_res == 0)
		printf("EVP_SealFinal Error: %s\n", ERR_error_string(ERR_get_error(), NULL));

	cipherlen += outlen;

	printf("encrypted_keys_len:%d\n",encrypted_keys_len[0]);
	send_data(sd,encrypted_keys[0], encrypted_keys_len[0]);
	//printf("encrypted_keys:%20s\n",encrypted_keys[0]);
	printf("iv_len:%d\n",EVP_CIPHER_iv_length(EVP_aes_128_cbc()));
	send_data(sd,iv,EVP_CIPHER_iv_length(EVP_aes_128_cbc()));
	//printf("cipherlen:%d\n\n",cipherlen);
 	send_data(sd,ciphertext,cipherlen);

 	EVP_CIPHER_CTX_cleanup(ctx);
	free(ctx);
	close(sd);

	return 0;
}