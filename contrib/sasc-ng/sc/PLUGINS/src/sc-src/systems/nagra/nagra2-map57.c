/*
 * Softcam plugin to VDR (C++)
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

// -- cN2Map57 ----------------------------------------------------------------

class cN2Map57 {
private:
  void mod_add(BIGNUM *arg1, BIGNUM *arg2, BIGNUM *arg3, BIGNUM *arg4);
  void bn_cmplx1(BIGNUM *arg1, BIGNUM *arg2, BIGNUM *arg3, BIGNUM *arg4, BIGNUM *arg5);
  void bn_cmplx1a(BIGNUM *arg1, BIGNUM *arg2, BIGNUM *arg3, BIGNUM *arg4, BIGNUM *arg5);
  void mod_sub(void);
  void bn_func1(BIGNUM *arg0);
  void bn_func2(int arg0);
  void bn_func3(int arg0);
  void bn_cmplx7(void);
  void bn_cmplx2(BIGNUM *var1, BIGNUM *var2, BIGNUM *var3, BIGNUM *var4, BIGNUM *var5, BIGNUM *var6);
  BIGNUM *bn_glb0, *bn_glb1, *bn_glb3, *bn_glb5, *bn_glb6, *bn_glb7;
  BIGNUM *bn_glb_a, *bn_glb_b, *bn_glb_c, *bn_glb_d, *bn_glb_e, *bn_glb_f, *bn_glb_g;
  BIGNUM *bn_glb_h, *bn_glb_i, *bn_glb_j, *bn_glb_k, *bn_glb_l, *bn_glb_m;
  BIGNUM *glb2pow128, *mask128, *glb2pow64, *mask64;
  BN_CTX *t1;
public:
  void Map57(unsigned char *data);
  };

void cN2Map57::mod_add(BIGNUM *arg1, BIGNUM *arg2, BIGNUM *arg3, BIGNUM *arg4)
{
  BN_add(arg1, arg2, arg3);
  if(BN_cmp(arg1, arg4) >= 0) {
    BN_sub(arg1, arg1, arg4);
  }
  BN_mask_bits(arg1, 128);
}

void cN2Map57::bn_cmplx1(BIGNUM *arg1, BIGNUM *arg2, BIGNUM *arg3, BIGNUM *arg4, BIGNUM *arg5)
{
  int j;
  BIGNUM *var44, *var64, *var84, *vara4;
  var44 = BN_new();
  var64 = BN_new();
  var84 = BN_new();
  vara4 = BN_new();
  BN_copy(var44, arg2);
  BN_copy(var64, arg3);
  BN_clear(vara4);
  for(j=0; j<2; j++) {
    BN_copy(var84, var64);
    BN_mask_bits(var84, 64);
    BN_rshift(var64, var64, 64);
    BN_mul(var84, var84, var44, t1);
    BN_add(vara4, vara4, var84);
    BN_copy(var84, vara4);
    BN_mask_bits(var84, 128);
    BN_mul(var84, vara4, arg4, t1);
    BN_mask_bits(var84, 64);
    BN_mul(var84, var84, arg5, t1);
    BN_add(vara4, vara4, var84);
    BN_rshift(vara4, vara4, 64);
    if(BN_cmp(vara4, arg5) >= 0) {
      BN_sub(vara4, vara4, arg5);
    }
    BN_mask_bits(vara4, 128);
  }
  BN_copy(arg1, vara4);
  BN_free(var44);
  BN_free(var64);
  BN_free(var84);
  BN_free(vara4);
}

void cN2Map57::bn_cmplx1a(BIGNUM *arg1, BIGNUM *arg2, BIGNUM *arg3, BIGNUM *arg4, BIGNUM *arg5)
{
  int j;
  BIGNUM *var44, *var64, *var84, *vara4;
  var44 = BN_new();
  var64 = BN_new();
  var84 = BN_new();
  vara4 = BN_new();
  BN_copy(var44, arg2);
  BN_copy(var64, arg3);
  BN_clear(vara4);
  for(j=0; j<2; j++) {
    BN_copy(var84, var64);
    BN_mask_bits(var84, 64);
    BN_rshift(var64, var64, 64);
    BN_mul(var84, var84, var44, t1);
    BN_add(vara4, vara4, var84);
    BN_copy(var84, vara4);
    BN_mask_bits(var84, 128);
    BN_mul(var84, vara4, arg4, t1);
    BN_mask_bits(var84, 64);
    BN_mul(var84, var84, arg5, t1);
    BN_add(vara4, vara4, var84);
    BN_rshift(vara4, vara4, 64);
    if(j==0 && BN_cmp(vara4, arg5) >= 0) {
      BN_sub(vara4, vara4, arg5);
    }
    BN_mask_bits(vara4, 128);
  }
  BN_copy(arg1, vara4);
  BN_free(var44);
  BN_free(var64);
  BN_free(var84);
  BN_free(vara4);
}

//uses 3, 1, glb2pow128
//sets 1, 0 (unused)
void cN2Map57::mod_sub()
{
  BN_copy(bn_glb0, bn_glb3);
  BN_mod_sub(bn_glb1, bn_glb3, bn_glb1, glb2pow128, t1);
  BN_mask_bits(bn_glb1, 128);
}

//uses 1, 3, 6
//sets  1, 0 (unused), 7(unused)
void cN2Map57::bn_func1(BIGNUM *arg0)
{
  BIGNUM *var30 = BN_new();
  BIGNUM *var50 = BN_new();
  BN_copy(var30,arg0);
  BN_mask_bits(var30, 8);
  unsigned int x = BN_get_word(var30);
  BN_copy(var30,arg0);
  if( x != 0) {
    BN_clear(var50);
    BN_set_word(var50, 2);
    BN_sub(var30, var30, var50);
  } else {
    BN_clear(var50);
    BN_set_word(var50, 0xfe);
    BN_add(var30, var30, var50);
  }
  BN_copy(bn_glb7, bn_glb1);
  if(BN_is_zero(arg0)) {
    BN_clear(bn_glb7);
    BN_set_word(bn_glb7, 1);
    BN_clear(bn_glb0);

    mod_add(bn_glb1, bn_glb7, bn_glb0, bn_glb3);
    BN_free(var30);
    BN_free(var50);
    return;
  } else {
    int msb = BN_num_bits(var30) -1;
    while (msb > 0) {

      bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);
      msb--;
      if(BN_is_bit_set(var30, msb)) {

        bn_cmplx1(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
      }
    }
    BN_clear(bn_glb7);
    BN_set_word(bn_glb7, 1);

    bn_cmplx1(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
    BN_clear(bn_glb0);
  }
  BN_free(var30);
  BN_free(var50);
}

//uses 3, 6, a, b, c, l, glb2pow128
//sets 0, 1, 5, 7, a, b, c, f, g
void cN2Map57::bn_func2(int arg0)
{
  BN_copy(bn_glb1, bn_glb_b);

  mod_add(bn_glb1, bn_glb1, bn_glb1, bn_glb3);
  BN_copy(bn_glb7, bn_glb1);
  BN_copy(bn_glb5, bn_glb_c);
  BN_mask_bits(bn_glb1, 128);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb_g, bn_glb1);
  BN_copy(bn_glb1, bn_glb7);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);
  BN_copy(bn_glb7, bn_glb_a);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  mod_sub();
  BN_copy(bn_glb_f, bn_glb1);
  BN_copy(bn_glb1, bn_glb7);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);
  BN_copy(bn_glb7, bn_glb1);

  mod_add(bn_glb1, bn_glb1, bn_glb1, bn_glb3);

  mod_add(bn_glb1, bn_glb1, bn_glb7, bn_glb3);
  BN_copy(bn_glb7, bn_glb1);
  BN_copy(bn_glb1, bn_glb_c);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);
  BN_copy(bn_glb5, bn_glb_l);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);

  mod_add(bn_glb1, bn_glb1, bn_glb7, bn_glb3);
  BN_copy(bn_glb7, bn_glb1);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);
  BN_copy(bn_glb0, bn_glb_f);

  mod_add(bn_glb1, bn_glb0, bn_glb1, bn_glb3);
  mod_add(bn_glb1, bn_glb0, bn_glb1, bn_glb3);
  if(arg0 == 0) {
    BN_copy(bn_glb_a, bn_glb1);
  } else {
    BN_copy(bn_glb_f, bn_glb1);
  }

  mod_add(bn_glb1, bn_glb0, bn_glb1, bn_glb3);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  BN_copy(bn_glb7, bn_glb1);
  BN_copy(bn_glb1, bn_glb_b);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);

  mod_add(bn_glb1, bn_glb1, bn_glb1, bn_glb3);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);

  mod_add(bn_glb1, bn_glb1, bn_glb1, bn_glb3);

  mod_add(bn_glb1, bn_glb1, bn_glb7, bn_glb3);
  mod_sub();
  if(arg0 == 0) {
    BN_copy(bn_glb_b, bn_glb1);
    BN_copy(bn_glb_c, bn_glb_g);
  } else {
    BN_copy(bn_glb_f, bn_glb1);
    BN_copy(bn_glb_f, bn_glb_g);
  }
}

//uses 3, 6, a, b, c, d, e, k
//sets 0, 1, 5, 7, a, b, c, f, g, h, i, j
void cN2Map57::bn_func3(int arg0)
{
  BN_copy(bn_glb1, bn_glb_c);
  BN_copy(bn_glb7, bn_glb1);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);

  bn_cmplx1(bn_glb0, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  BN_copy(bn_glb_f, bn_glb0);
  BN_copy(bn_glb5, bn_glb_d);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb7, bn_glb1);
  mod_sub();
  BN_copy(bn_glb0, bn_glb_a);

  mod_add(bn_glb1, bn_glb0, bn_glb1, bn_glb3);
  BN_copy(bn_glb_g, bn_glb1);
  BN_copy(bn_glb5, bn_glb_c);

  bn_cmplx1(bn_glb0, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  if(arg0 == 0) {
    BN_copy(bn_glb_c, bn_glb0);
  } else {
    BN_copy(bn_glb_g, bn_glb0);
  }

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);
  BN_copy(bn_glb_h, bn_glb1);
  BN_copy(bn_glb0, bn_glb_a);

  mod_add(bn_glb0, bn_glb0, bn_glb7, bn_glb3);
  BN_copy(bn_glb7, bn_glb0);

  //NOTE: don't 'mod' bn_glb1, but DO 'mod' glb_i
  bn_cmplx1(bn_glb7, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  bn_cmplx1a(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  BN_copy(bn_glb_i, bn_glb7);
  BN_copy(bn_glb1, bn_glb_e);
  BN_copy(bn_glb5, bn_glb_f);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb_f, bn_glb1);
  mod_sub();
  BN_copy(bn_glb0, bn_glb_b);

  mod_add(bn_glb1, bn_glb0, bn_glb1, bn_glb3);
  BN_copy(bn_glb_j, bn_glb1);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);
  BN_copy(bn_glb0, bn_glb1);
  BN_copy(bn_glb1, bn_glb7);
  BN_copy(bn_glb7, bn_glb0);
  mod_sub();

  mod_add(bn_glb1, bn_glb1, bn_glb7, bn_glb3);
  if(arg0 == 0) {
    BN_copy(bn_glb_a, bn_glb1);
  } else {
    BN_copy(bn_glb_f, bn_glb1);
  }

  mod_add(bn_glb1, bn_glb1, bn_glb1, bn_glb3);
  mod_sub();
  BN_copy(bn_glb7, bn_glb_i);

  mod_add(bn_glb1, bn_glb1, bn_glb7, bn_glb3);
  BN_copy(bn_glb5, bn_glb_j);

  bn_cmplx1(bn_glb0, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb1, bn_glb_f);
  BN_copy(bn_glb_f, bn_glb0);
  BN_copy(bn_glb7, bn_glb_b);

  mod_add(bn_glb1, bn_glb1, bn_glb7, bn_glb3);
  BN_copy(bn_glb7, bn_glb1);
  BN_copy(bn_glb1, bn_glb_g);
  BN_copy(bn_glb5, bn_glb_h);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  mod_sub();
  BN_copy(bn_glb7, bn_glb_f);

  mod_add(bn_glb1, bn_glb1, bn_glb7, bn_glb3);
  BN_copy(bn_glb5, bn_glb_k);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  if(arg0 == 0) {
    BN_copy(bn_glb_b, bn_glb1);
  } else {
    BN_copy(bn_glb_f, bn_glb1);
  }
}

//uses c, d, e, m
//sets 0, a, b, c
void cN2Map57::bn_cmplx7()
{
  BIGNUM *var1;
  var1 = BN_new();
  BN_copy(bn_glb0, bn_glb_c);
  if(BN_is_zero(bn_glb_c)) {
    BN_copy(bn_glb_a, bn_glb_d);
    BN_copy(bn_glb_b, bn_glb_e);
    BN_copy(bn_glb_c, bn_glb_m);
    bn_func3(1);
  } else {
    BN_clear(var1);
    BN_set_word(var1, 0xFFFFFFFF);
    BN_mask_bits(bn_glb_a, 32);
    BN_lshift(var1, bn_glb_m, 0x20);
    BN_add(bn_glb_a, bn_glb_a, var1);
    BN_mask_bits(bn_glb_a, 128);
    bn_func3(0);
  }
  BN_free(var1);
}

void cN2Map57::bn_cmplx2(BIGNUM *var1, BIGNUM *var2, BIGNUM *var3, BIGNUM *var4, BIGNUM *var5, BIGNUM *var6)
{
  BIGNUM *var48;
  int len = BN_num_bits(var6);
  int i;
  if(len < 2)
    return;

  if(BN_is_zero(var2) && BN_is_zero(var3) && BN_is_zero(var4))
    return;
  var48 = BN_new();
  BN_copy(bn_glb3, var1);

  BN_copy(bn_glb6, bn_glb3);
  BN_set_bit(bn_glb6, 0);
  BN_sub(bn_glb6, glb2pow128, bn_glb6);
  BN_mod_inverse(bn_glb6, bn_glb6, glb2pow64, t1);
  BN_clear(bn_glb0);
  //
  if(! BN_is_zero(bn_glb3)) {
    BN_clear(bn_glb1);
    BN_set_word(bn_glb1, 2);
    BN_clear(bn_glb_k);
    BN_set_word(bn_glb_k, 0x88);
    BN_mod_exp(bn_glb1, bn_glb1, bn_glb_k, bn_glb3, t1);
  }
  //
  for(i=0; i < 4; i++) {

    bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);
  }
  //
  BN_clear(bn_glb7);
  BN_set_word(bn_glb7, 1);
  BN_add(bn_glb0, bn_glb3, bn_glb7);
  BN_copy(bn_glb_k, bn_glb0);
  BN_rshift(bn_glb_k, bn_glb_k, 1);
  BN_copy(bn_glb7, bn_glb1);
  BN_copy(bn_glb5, bn_glb_k);
  BN_mask_bits(bn_glb5, 128);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb_k, bn_glb1);

  BN_copy(bn_glb1, var5);
  BN_mask_bits(bn_glb1, 128);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  BN_copy(bn_glb_l, bn_glb1);
  BN_copy(bn_glb1, bn_glb7);
  BN_clear(bn_glb5);
  BN_set_word(bn_glb5, 1);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb_c, bn_glb1);
  BN_copy(bn_glb_m, bn_glb1);
  BN_copy(bn_glb1, bn_glb7);

  BN_copy(bn_glb5, var2);
  BN_mask_bits(bn_glb5, 128);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb_a, bn_glb1);
  BN_copy(bn_glb1, bn_glb7);

  BN_copy(bn_glb5, var3);
  BN_mask_bits(bn_glb5, 128);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb_b, bn_glb1);
  BN_copy(bn_glb_d, bn_glb_a);
  BN_copy(bn_glb_e, bn_glb_b);

  int x = len -1;
  while(x > 0) {
    x--;
    bn_func2(0);
    if(BN_is_bit_set(var6, x)) {
      bn_cmplx7();
    }
  }

  BN_copy(bn_glb1, bn_glb_c);
  BN_mask_bits(bn_glb1, 128);
  BN_copy(bn_glb7, bn_glb1);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb1, bn_glb6, bn_glb3);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  BN_clear(bn_glb7);
  BN_set_word(bn_glb7, 1);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb7, bn_glb6, bn_glb3);
  BN_copy(bn_glb0, bn_glb1);
  BN_clear(bn_glb7);
  BN_set_word(bn_glb7, 1);
  BN_copy(bn_glb1, bn_glb0);
  BN_clear(bn_glb0);
  bn_func1(var1);
  BN_copy(bn_glb5, bn_glb_b);
  BN_mask_bits(bn_glb5, 128);

  bn_cmplx1(bn_glb0, bn_glb1, bn_glb5, bn_glb6, bn_glb3);

  BN_copy(bn_glb7, bn_glb0);
  BN_copy(bn_glb5, bn_glb_c);
  BN_mask_bits(bn_glb5, 128);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);

  BN_copy(bn_glb5, bn_glb_a);
  BN_mask_bits(bn_glb5, 128);

  bn_cmplx1(bn_glb1, bn_glb1, bn_glb5, bn_glb6, bn_glb3);

  BN_clear(bn_glb5);
  BN_set_word(bn_glb5, 1);

  bn_cmplx1(bn_glb0, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb_a, bn_glb0);
  BN_copy(bn_glb1, bn_glb7);

  BN_clear(bn_glb5);
  BN_set_word(bn_glb5, 1);

  bn_cmplx1(bn_glb0, bn_glb1, bn_glb5, bn_glb6, bn_glb3);
  BN_copy(bn_glb_b, bn_glb0);
  BN_free(var48);
}
 
void cN2Map57::Map57(unsigned char *data)
{
  BIGNUM *var38, *var58, *var78, *var98, *varb8, *vard8;
  BN_CTX *t;
  unsigned char tmpdata[256];
  unsigned char res[256];

  t = BN_CTX_new();
  t1 = BN_CTX_new();
  BN_CTX_init(t);

  glb2pow128 = BN_new();
  BN_clear(glb2pow128);
  BN_set_bit(glb2pow128, 128);
  mask128 = BN_new();
  BN_hex2bn(&mask128, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");

  glb2pow64 = BN_new();
  BN_clear(glb2pow64);
  BN_set_bit(glb2pow64, 64);
  mask64 = BN_new();
  BN_hex2bn(&mask64, "FFFFFFFFFFFFFFFF");
  
  bn_glb0=BN_new(); BN_clear(bn_glb0);
  bn_glb1=BN_new(); BN_clear(bn_glb1);
  bn_glb3=BN_new(); BN_clear(bn_glb3);
  bn_glb5=BN_new(); BN_clear(bn_glb5);
  bn_glb6=BN_new(); BN_clear(bn_glb6);
  bn_glb7=BN_new(); BN_clear(bn_glb7);

  bn_glb_a=BN_new(); BN_clear(bn_glb_a);
  bn_glb_b=BN_new(); BN_clear(bn_glb_b);
  bn_glb_c=BN_new(); BN_clear(bn_glb_c);
  bn_glb_d=BN_new(); BN_clear(bn_glb_d);
  bn_glb_e=BN_new(); BN_clear(bn_glb_e);
  bn_glb_f=BN_new(); BN_clear(bn_glb_f);
  bn_glb_g=BN_new(); BN_clear(bn_glb_g);
  bn_glb_h=BN_new(); BN_clear(bn_glb_h);
  bn_glb_i=BN_new(); BN_clear(bn_glb_i);
  bn_glb_j=BN_new(); BN_clear(bn_glb_j);
  bn_glb_k=BN_new(); BN_clear(bn_glb_k);
  bn_glb_l=BN_new(); BN_clear(bn_glb_l);
  bn_glb_m=BN_new(); BN_clear(bn_glb_m);

  var38=BN_new(); BN_clear(var38);
  var58=BN_new(); BN_clear(var58);
  var78=BN_new(); BN_clear(var78);
  var98=BN_new(); BN_clear(var98);
  varb8=BN_new(); BN_clear(varb8);
  vard8=BN_new(); BN_clear(vard8);

  memcpy(tmpdata, data, 0x60);
  RotateBytes(tmpdata, 0x60);
  BN_bin2bn(tmpdata, 16, var98);
  BN_bin2bn(tmpdata+0x10, 16, var78);
  BN_bin2bn(tmpdata+0x20, 16, var38);
  BN_bin2bn(tmpdata+0x30, 16, vard8);
  BN_bin2bn(tmpdata+0x40, 16, var58);
  BN_bin2bn(tmpdata+0x50, 16, varb8);

  bn_cmplx2(varb8, var58, vard8, var38, var78, var98);

  memset(res, 0, 0x80);
  unsigned int *dest = (unsigned int *)res, *src = (unsigned int *)data;
  *dest++ = src[0x03];
  *dest++ = src[0x02];
  *dest++ = src[0x01];
  *dest++ = src[0x00];
  *dest++ = src[0x07];
  *dest++ = src[0x06];
  *dest++ = src[0x05];
  *dest++ = src[0x04];

  memset(tmpdata, 0, 0x20);
  int len = BN_bn2bin(bn_glb_a, tmpdata);
  if(len) {
    RotateBytes(tmpdata, len);
  }
  src = (unsigned int *)tmpdata;
  *dest++ = src[0x03];
  *dest++ = src[0x02];
  *dest++ = src[0x01];
  *dest++ = src[0x00];

  memset(tmpdata, 0, 0x20);
  len = BN_bn2bin(bn_glb_m, tmpdata);
  if(len) {
    RotateBytes(tmpdata, len);
  }
  *dest = src[0x03];
  dest+=4;

  memset(tmpdata, 0, 0x20);
  len = BN_bn2bin(bn_glb_b, tmpdata);
  if(len) {
    RotateBytes(tmpdata, len);
  }
  *dest++ = src[0x03];
  *dest++ = src[0x02];
  *dest++ = src[0x01];
  *dest++ = src[0x00];

  dest+=4;
  src = (unsigned int *)(data+0x60);
  *dest++ = src[0x03];
  *dest++ = src[0x02];
  *dest++ = src[0x01];
  *dest++ = src[0x00];
  *dest++ = src[0x07];
  *dest++ = src[0x06];
  *dest++ = src[0x05];
  *dest++ = src[0x04];

  *(unsigned int *)(data + (0<<2))= *(unsigned int *)(res + (11<<2));
  *(unsigned int *)(data + (1<<2))= *(unsigned int *)(res + (10<<2));
  *(unsigned int *)(data + (2<<2))= *(unsigned int *)(res + (9<<2));
  *(unsigned int *)(data + (3<<2))= *(unsigned int *)(res + (8<<2));
  *(unsigned int *)(data + (4<<2))= *(unsigned int *)(res + (12<<2));
  *(unsigned int *)(data + (5<<2))= *(unsigned int *)(res + (13<<2));
  *(unsigned int *)(data + (6<<2))= *(unsigned int *)(res + (14<<2));
  *(unsigned int *)(data + (7<<2))= *(unsigned int *)(res + (15<<2));
  *(unsigned int *)(data + (8<<2))= *(unsigned int *)(res + (19<<2));
  *(unsigned int *)(data + (9<<2))= *(unsigned int *)(res + (18<<2));
  *(unsigned int *)(data + (10<<2))= *(unsigned int *)(res + (17<<2));
  *(unsigned int *)(data + (11<<2))= *(unsigned int *)(res + (16<<2));
  *(unsigned int *)(data + (12<<2))= *(unsigned int *)(res + (20<<2));
  *(unsigned int *)(data + (13<<2))= *(unsigned int *)(res + (21<<2));
  *(unsigned int *)(data + (14<<2))= *(unsigned int *)(res + (22<<2));
  *(unsigned int *)(data + (15<<2))= *(unsigned int *)(res + (23<<2));

  BN_free(glb2pow128);
  BN_free(mask128);
  BN_free(glb2pow64);
  BN_free(mask64);
  
  BN_free(bn_glb0);
  BN_free(bn_glb1);
  BN_free(bn_glb3);
  BN_free(bn_glb5);
  BN_free(bn_glb6);
  BN_free(bn_glb7);

  BN_free(bn_glb_a);
  BN_free(bn_glb_b);
  BN_free(bn_glb_c);
  BN_free(bn_glb_d);
  BN_free(bn_glb_e);
  BN_free(bn_glb_f);
  BN_free(bn_glb_g);
  BN_free(bn_glb_h);
  BN_free(bn_glb_i);
  BN_free(bn_glb_j);
  BN_free(bn_glb_k);
  BN_free(bn_glb_l);
  BN_free(bn_glb_m);

  BN_free(var38);
  BN_free(var58);
  BN_free(var78);
  BN_free(var98);
  BN_free(varb8);
  BN_free(vard8);

  BN_CTX_free(t);
  BN_CTX_free(t1);
}
