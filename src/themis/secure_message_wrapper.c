/**
 * @file
 *
 * (c) CossackLabs
 */

#include <string.h>


#include <common/error.h>
#include <soter/soter.h>
#include <soter/soter_rsa_key.h>
#include <soter/soter_ec_key.h>
#include <soter/soter_asym_sign.h>
#include <themis/secure_message_wrapper.h>


#define THEMIS_RSA_SYMM_PASSWD_LENGTH 70 //!!! need to aprove
#define THEMIS_RSA_SYMM_ENCRYPTED_PASSWD_LENGTH 256 //encrypted password for rsa 256 
#define THEMIS_RSA_SYMM_SALT_LENGTH 16

struct themis_secure_message_hdr_type{
  uint32_t message_type;
  uint32_t message_length;
};
typedef struct themis_secure_message_hdr_type themis_secure_message_hdr_t;

#define THEMIS_SECURE_MESSAGE_LENGTH(hdr) (sizeof(themis_secure_message_hdr_t)+hdr->message_length)

struct themis_secure_signed_message_hdr_type{
  themis_secure_message_hdr_t message_hdr;
  uint32_t signature_length;
};

typedef struct themis_secure_signed_message_hdr_type themis_secure_signed_message_hdr_t; 

soter_sign_alg_t get_alg_id(const uint8_t* key, size_t key_length)
{
  if (key_length < sizeof(soter_container_hdr_t) && key_length < ((const soter_container_hdr_t*)key)->size){
    return (soter_sign_alg_t)(-1);
  }
  if (memcmp(((const soter_container_hdr_t*)key)->tag,EC_PRIV_KEY_PREF,3)==0 || memcmp(((const soter_container_hdr_t*)key)->tag,EC_PUB_KEY_PREF,3)==0){
      return SOTER_SIGN_ecdsa_none_pkcs8;
  }
  if (memcmp(((const soter_container_hdr_t*)key)->tag,RSA_PRIV_KEY_PREF,3)==0 || memcmp(((const soter_container_hdr_t*)key)->tag,RSA_PUB_KEY_PREF,3)==0){
      return SOTER_SIGN_rsa_pss_pkcs8;
  }
  return (soter_sign_alg_t)(-1);
}

themis_secure_message_signer_t* themis_secure_message_signer_init(const uint8_t* key, const size_t key_length)
{
  themis_secure_message_signer_t* ctx=malloc(sizeof(themis_secure_message_signer_t));
  if(!ctx){
    return NULL;
  }
  ctx->sign_ctx=NULL;
  ctx->sign_ctx=soter_sign_create(get_alg_id(key,key_length),NULL,0,key, key_length);
  if(!(ctx->sign_ctx)){
    free(ctx);
    return NULL;
  }
  return ctx;
}

themis_status_t themis_secure_message_signer_proceed(themis_secure_message_signer_t* ctx, const uint8_t* message, const size_t message_length, uint8_t* wrapped_message, size_t* wrapped_message_length)
{
  HERMES_CHECK(ctx!=NULL && ctx->sign_ctx!=NULL);
  HERMES_CHECK(message!=NULL && message_length!=0 && wrapped_message_length!=NULL);
  uint8_t* signature=NULL;
  size_t signature_length=0;
  //  if(!ctx->precompute_signature_present){
    HERMES_CHECK(soter_sign_update(ctx->sign_ctx, message, message_length)==HERMES_SUCCESS);
    //}
  HERMES_CHECK(soter_sign_final(ctx->sign_ctx, signature, &signature_length)==HERMES_BUFFER_TOO_SMALL);
  if((message_length+signature_length+sizeof(themis_secure_message_hdr_t)>(*wrapped_message_length))){
    (*wrapped_message_length)=message_length+signature_length+sizeof(themis_secure_message_hdr_t);
    return HERMES_BUFFER_TOO_SMALL;
  }
  signature=malloc(signature_length);
  HERMES_CHECK(signature!=NULL);
  if(soter_sign_final(ctx->sign_ctx, signature, &signature_length)!=HERMES_SUCCESS){
    free(signature);
    return HERMES_FAIL;
  }
  themis_secure_signed_message_hdr_t hdr;
  switch(soter_sign_get_alg_id(ctx->sign_ctx)){
  case SOTER_SIGN_ecdsa_none_pkcs8:
    hdr.message_hdr.message_type=THEMIS_SECURE_MESSAGE_EC_SIGNED;
    break;
  case SOTER_SIGN_rsa_pss_pkcs8:
    hdr.message_hdr.message_type=THEMIS_SECURE_MESSAGE_RSA_SIGNED;
    break;
  default:
    return HERMES_INVALID_PARAMETER;  
  };
  hdr.message_hdr.message_length=message_length;
  hdr.signature_length=signature_length;
  memcpy(wrapped_message,&hdr,sizeof(themis_secure_message_hdr_t));
  memcpy(wrapped_message+sizeof(themis_secure_message_hdr_t),message,message_length);
  memcpy(wrapped_message+sizeof(themis_secure_message_hdr_t)+message_length,signature,signature_length);
  (*wrapped_message_length)=message_length+signature_length+sizeof(themis_secure_message_hdr_t);
  return HERMES_SUCCESS;
}

themis_status_t secure_message_signer_destroy(themis_secure_message_signer_t* ctx)
{
  soter_sign_destroy(ctx->sign_ctx);
  ctx->sign_ctx=NULL;
  free(ctx);
  return HERMES_SUCCESS;
}

themis_secure_message_verifier_t* themis_secure_message_verifier_init(const uint8_t* key, const size_t key_length)
{
  themis_secure_message_verifier_t* ctx=malloc(sizeof(themis_secure_message_verifier_t));
  if(!ctx){
    return NULL;
  }
  ctx->verify_ctx=soter_verify_create(get_alg_id(key,key_length),key, key_length, NULL,0);
  if(!ctx->verify_ctx){
    free(ctx);
    return NULL;
  }
  return ctx;
}
 
themis_status_t themis_secure_message_verifier_proceed(themis_secure_message_verifier_t* ctx, const uint8_t* wrapped_message, const size_t wrapped_message_length, uint8_t* message, size_t* message_length)
{
  HERMES_CHECK(ctx!=NULL);
  HERMES_CHECK(wrapped_message!=NULL && wrapped_message_length!=0 && message_length!=NULL);
  themis_secure_signed_message_hdr_t* msg=(themis_secure_signed_message_hdr_t*)wrapped_message;
  if(((msg->message_hdr.message_type==THEMIS_SECURE_MESSAGE_RSA_SIGNED && soter_verify_get_alg_id(ctx->verify_ctx)!=SOTER_SIGN_rsa_pss_pkcs8)
      || (msg->message_hdr.message_type==THEMIS_SECURE_MESSAGE_EC_SIGNED && soter_verify_get_alg_id(ctx->verify_ctx)!=SOTER_SIGN_ecdsa_none_pkcs8))
     && (msg->message_hdr.message_length+msg->signature_length+sizeof(themis_secure_message_hdr_t)>wrapped_message_length)){
    return HERMES_INVALID_PARAMETER;
  }
  if((*message_length)<msg->message_hdr.message_length){
    (*message_length)=msg->message_hdr.message_length;
    return HERMES_BUFFER_TOO_SMALL;
  }
  HERMES_CHECK(soter_verify_update(ctx->verify_ctx, wrapped_message+sizeof(themis_secure_message_hdr_t), msg->message_hdr.message_length)==HERMES_SUCCESS);
  HERMES_CHECK(soter_verify_final(ctx->verify_ctx, wrapped_message+sizeof(themis_secure_message_hdr_t)+msg->message_hdr.message_length, msg->signature_length)==HERMES_SUCCESS);
  memcpy(message,wrapped_message+sizeof(themis_secure_message_hdr_t),msg->message_hdr.message_length);
  (*message_length)=msg->message_hdr.message_length;
  return HERMES_SUCCESS;
}

themis_status_t secure_message_verifier_destroy(themis_secure_message_verifier_t* ctx){
  soter_verify_destroy(ctx->verify_ctx);
  free(ctx);
  return HERMES_SUCCESS;  
}

typedef struct symm_init_ctx_type{
  uint8_t passwd[THEMIS_RSA_SYMM_PASSWD_LENGTH];
  uint8_t salt[THEMIS_RSA_SYMM_SALT_LENGTH];
} symm_init_ctx_t;

struct themis_secure_message_rsa_encrypt_worker_type{
  soter_asym_cipher_t* asym_cipher;
  //  uint8_t encrypted_symm_passwd[THEMIS_RSA_SYMM_ENCRYPTED_PASSWD_LENGTH];
};

typedef struct themis_secure_encrypted_message_hdr_type{
  themis_secure_message_hdr_t message_hdr;
} themis_secure_encrypted_message_hdr_t;

themis_secure_message_rsa_encrypter_t* themis_secure_message_rsa_encrypter_init(const uint8_t* peer_public_key, const size_t peer_public_key_length){
  HERMES_CHECK_PARAM_(peer_public_key==NULL);
  HERMES_CHECK_PARAM_(peer_public_key_length==0);
  themis_secure_message_rsa_encrypter_t* ctx=malloc(sizeof(themis_secure_message_rsa_encrypter_t));
  HERMES_CHECK_(ctx!=NULL);
  ctx->asym_cipher=soter_asym_cipher_create(SOTER_ASYM_CIPHER_OAEP);
  HERMES_FAIL_IF_(ctx->asym_cipher!=NULL, secure_message_rsa_encrypter_destroy(ctx));
  HERMES_FAIL_IF(soter_asym_cipher_import_key(ctx->asym_cipher, peer_public_key, peer_public_key_length)!=HERMES_SUCCESS, secure_message_rsa_encrypter_destroy(ctx));
  return ctx;
}

themis_status_t themis_secure_message_rsa_encrypter_proceed(themis_secure_message_rsa_encrypter_t* ctx, const uint8_t* message, const size_t message_length, uint8_t* wrapped_message, size_t* wrapped_message_length){
  if((*wrapped_message_length)<(sizeof(themis_secure_encrypted_message_hdr_t)+THEMIS_RSA_SYMM_ENCRYPTED_PASSWD_LENGTH+message_length)){
    (*wrapped_message_length)=(sizeof(themis_secure_encrypted_message_hdr_t)+THEMIS_RSA_SYMM_ENCRYPTED_PASSWD_LENGTH+message_length);
    return HERMES_BUFFER_TOO_SMALL;
  }
  symm_init_ctx_t symm_passwd_salt;
  HERMES_CHECK(soter_rand((uint8_t*)(&symm_passwd_salt), sizeof(symm_passwd_salt))!=HERMES_SUCCESS);
  soter_sym_ctx_t* cipher=soter_aes_ctr_encrypt_create(PBKDF2, symm_passwd_salt.passwd, THEMIS_RSA_SYMM_PASSWD_LENGTH, symm_passwd_salt.salt, THEMIS_RSA_SYMM_SALT_LENGTH);
  HERMES_CHECK(cipher!=NULL);
  size_t symm_passwd_length=THEMIS_RSA_SYMM_ENCRYPTED_PASSWD_LENGTH;
  HERMES_FAIL_IF(soter_asym_cipher_encrypt(ctx->asym_cipher, (uint8_t*)(&symm_passwd_salt), sizeof(symm_passwd_salt), wrapped_message+sizeof(themis_secure_encrypted_message_hdr_t), &symm_passwd_length)!=HERMES_SUCCESS, soter_aes_ctr_encrypt_destroy(cipher));
  HERMES_FAIL_IF(soter_aes_ctr_encrypt_update(cipher, message, message_length, wrapped_message+sizeof(themis_secure_encrypted_message_hdr_t)+THEMIS_RSA_SYMM_ENCRYPTED_PASSWD_LENGTH, wrapped_message_length), soter_aes_ctr_encrypt_destroy(cipher));
  (*wrapped_message_length)+=sizeof(themis_secure_encrypted_message_hdr_t)+THEMIS_RSA_SYMM_ENCRYPTED_PASSWD_LENGTH;
  ((themis_secure_encrypted_message_hdr_t*)wrapped_message)->message_hdr.message_type=THEMIS_SECURE_MESSAGE_RSA_ENCRYPTED;
  ((themis_secure_encrypted_message_hdr_t*)wrapped_message)->message_hdr.message_length=(*wrapped_message_length);  
  return HERMES_SUCCESS;
}

themis_status_t secure_message_rsa_encrypter_destroy(themis_secure_message_rsa_encrypter_t* ctx){
  HERMES_CHECK_PARAM(ctx!=NULL);
  if(ctx->asym_cipher!=NULL){
    soter_asim_cipher_destroy(ctx->asym_cipher);
  }
  free(ctx);
  return HERMES_SUCCESS;
}

themis_secure_message_rsa_decrypter_t* themis_secure_message_rsa_decrypter_init(const uint8_t* private_key, const size_t private_key_length){
}
themis_status_t themis_secure_message_rsa_decrypter_proceed(themis_secure_message_rsa_decrypter_t* ctx, const uint8_t* message, const size_t message_length, uint8_t* wrapped_message, size_t* wrapped_message_length){
}
themis_status_t secure_message_rsa_decrypter_destroy(themis_secure_message_rsa_decrypter_t* ctx){
}

themis_secure_message_ec_encrypter_t* themis_secure_message_ec_encrypter_init(const uint8_t* private_key, const size_t private_key_length, const uint8_t* peer_public_key, const size_t peer_public_key_length){
}
themis_status_t themis_secure_message_ec_encrypter_proceed(themis_secure_message_ec_encrypter_t* ctx, const uint8_t* message, const size_t message_length, uint8_t* wrapped_message, size_t* wrapped_message_length){
}
themis_status_t secure_message_ec_encrypter_destroy(themis_secure_message_ec_encrypter_t* ctx){
}

themis_secure_message_ec_decrypter_t* themis_secure_message_ec_decrypter_init(const uint8_t* private_key, const size_t private_key_length, const uint8_t* peer_public_key, const size_t peer_public_key_length){
}
themis_status_t themis_secure_message_ec_decrypter_proceed(themis_secure_message_ec_decrypter_t* ctx, const uint8_t* message, const size_t message_length, uint8_t* wrapped_message, size_t* wrapped_message_length){
}
themis_status_t secure_message_ec_decrypter_destroy(themis_secure_message_ec_decrypter_t* ctx){
}

