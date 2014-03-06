
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_security_Signer__
#define __java_security_Signer__

#pragma interface

#include <java/security/Identity.h>
extern "Java"
{
  namespace java
  {
    namespace security
    {
        class IdentityScope;
        class KeyPair;
        class PrivateKey;
        class Signer;
    }
  }
}

class java::security::Signer : public ::java::security::Identity
{

public: // actually protected
  Signer();
public:
  Signer(::java::lang::String *);
  Signer(::java::lang::String *, ::java::security::IdentityScope *);
  virtual ::java::security::PrivateKey * getPrivateKey();
  virtual void setKeyPair(::java::security::KeyPair *);
  virtual ::java::lang::String * toString();
private:
  static const jlong serialVersionUID = -1763464102261361480LL;
  ::java::security::PrivateKey * __attribute__((aligned(__alignof__( ::java::security::Identity)))) privateKey;
public:
  static ::java::lang::Class class$;
};

#endif // __java_security_Signer__
