#include <stdlib.h>
#include <string.h>
#include "stats_tools.h"
#include "../src/dct.h"
#include "../src/intra.h"

void mode_data_init(mode_data *_md){
  int i;
  int j;
  _md->n=0;
  _md->mean=0;
  _md->var=0;
  for(i=0;i<B_SZ*B_SZ;i++){
    _md->satd_avg[i]=0;
    _md->ref_mean[i]=0;
    _md->pred_mean[i]=0;
    for(j=0;j<B_SZ*B_SZ;j++){
      _md->ref_cov[i][j]=0;
      _md->pred_cov[i][j]=0;
    }
  }
}

/* update the input mean and variance */
void mode_data_add_input(mode_data *_md,const unsigned char *_data,int _stride){
  int n;
  int i;
  int j;
  n=(_md->n-1)*B_SZ*B_SZ;
  for(i=0;i<B_SZ;i++){
    for(j=0;j<B_SZ;j++){
      double delta;
      n++;
      delta=_data[_stride*i+j]*INPUT_SCALE-_md->mean;
      _md->mean+=delta/n;
      _md->var+=delta*delta*(n-1)/n;
    }
  }
}

void mode_data_add_block(mode_data *_md,const od_coeff *_block,int _stride,
 int _ref){
  double *mean;
  double(*cov)[B_SZ*B_SZ];
  double  delta[B_SZ*B_SZ];
  int     i;
  int     j;
  mean=_ref?_md->ref_mean:_md->pred_mean;
  cov=_ref?_md->ref_cov:_md->pred_cov;
  for(j=0;j<B_SZ;j++){
    for(i=0;i<B_SZ;i++){
      delta[B_SZ*j+i]=_block[_stride*j+i]-mean[B_SZ*j+i];
      mean[B_SZ*j+i]+=delta[B_SZ*j+i]/_md->n;
    }
  }
  for(i=0;i<B_SZ*B_SZ;i++){
    for(j=0;j<B_SZ*B_SZ;j++){
      cov[i][j]+=delta[i]*delta[j]*(_md->n-1)/_md->n;
    }
  }
}

void mode_data_combine(mode_data *_a,const mode_data *_b){
  double s;
  double delta;
  int    i;
  int    j;
  double delta_ref[B_SZ*B_SZ];
  double delta_pred[B_SZ*B_SZ];
  s=((double)_b->n)/(_a->n+_b->n);
  delta=_b->mean-_a->mean;
  _a->mean+=delta*s;
  for(i=0;i<B_SZ*B_SZ;i++){
    _a->satd_avg[i]+=(_b->satd_avg[i]-_a->satd_avg[i])*s;
    delta_ref[i]=_b->ref_mean[i]-_a->ref_mean[i];
    _a->ref_mean[i]+=delta_ref[i]*s;
    delta_pred[i]=_b->pred_mean[i]-_a->pred_mean[i];
    _a->pred_mean[i]+=delta_pred[i]*s;
  }
  s*=_a->n;
  _a->var+=_b->var+delta*s;
  for(i=0;i<B_SZ*B_SZ;i++){
    for(j=0;j<B_SZ*B_SZ;j++){
      _a->ref_cov[i][j]+=_b->ref_cov[i][j]+delta_ref[i]*delta_ref[j]*s;
      _a->pred_cov[i][j]+=_b->pred_cov[i][j]+delta_pred[i]*delta_pred[j]*s;
    }
  }
  _a->n+=_b->n;
}

void mode_data_correct(mode_data *_md){
  int i;
  int j;
  _md->var/=_md->n*B_SZ*B_SZ;
  for(i=0;i<B_SZ*B_SZ;i++){
    for(j=0;j<B_SZ*B_SZ;j++){
      _md->ref_cov[i][j]/=_md->n;
      _md->pred_cov[i][j]/=_md->n;
    }
  }
}

void mode_data_print(mode_data *_md,const char *_label,double *_scale){
  double     cg_ref;
  double     cg_pred;
  double     pg;
  int        j;
  int        i;
  double     satd_avg;
  double     bits_avg;
  cg_ref=10*log10(_md->var);
  cg_pred=10*log10(_md->var);
  pg=0;
  satd_avg=0;
  bits_avg=0;
  for(j=0;j<B_SZ;j++){
    for(i=0;i<B_SZ;i++){
      int    x;
      double b;
      x=B_SZ*j+i;
      cg_ref-=10*log10(_md->ref_cov[x][x]*_scale[j]*_scale[i])/(B_SZ*B_SZ);
      cg_pred-=10*log10(_md->pred_cov[x][x]*_scale[j]*_scale[i])/(B_SZ*B_SZ);
      /* compute prediction gain 10*log10(prod_j(ref_cov[j]/pred_cov[j])) */
      pg+=10*log10(_md->ref_cov[x][x]/_md->pred_cov[x][x])/(B_SZ*B_SZ);
      satd_avg+=sqrt(_scale[i]*_scale[j])*_md->satd_avg[x];
      b=sqrt(_scale[j]*_scale[i]*_md->pred_cov[x][x]/2);
      bits_avg+=1+OD_LOG2(b)+M_LOG2E/b*_md->satd_avg[x];
    }
  }
  printf("%s Blocks %5i SATD %G Bits %G Mean %G Var %G CgRef %G CgPred %G Pg %G\n",
   _label,_md->n,satd_avg,bits_avg,_md->mean,_md->var,cg_ref,cg_pred,pg);
}

void intra_stats_init(intra_stats *_this){
  int mode;
  mode_data_init(&_this->fr);
  for(mode=0;mode<OD_INTRA_NMODES;mode++){
    mode_data_init(&_this->md[mode]);
  }
}

void intra_stats_correct(intra_stats *_this){
  int mode;
  mode_data_correct(&_this->fr);
  for(mode=0;mode<OD_INTRA_NMODES;mode++){
    mode_data_correct(&_this->md[mode]);
  }
}

void intra_stats_print(intra_stats *_this,const char *_label,
 double *_scale){
  int mode;
  printf("%s\n",_label);
  for(mode=0;mode<OD_INTRA_NMODES;mode++){
    char label[16];
    sprintf(label,"Mode %i",mode);
    mode_data_print(&_this->md[mode],label,_scale);
  }
  mode_data_print(&_this->fr,"Pooled",_scale);
}

void intra_stats_combine(intra_stats *_this,const intra_stats *_that){
  int mode;
  mode_data_combine(&_this->fr,&_that->fr);
  for(mode=0;mode<OD_INTRA_NMODES;mode++){
    mode_data_combine(&_this->md[mode],&_that->md[mode]);
  }
}

/* compute the scale factors for the DCT and TDLT transforms */
double VP8_SCALE[B_SZ];
double OD_SCALE[B_SZ];

#define SCALE_BITS (14)
#define PRINT_SCALE (0)

void vp8_scale_init(double _vp8_scale[B_SZ]){
  int j;
  int i;
  od_coeff buf[B_SZ];
  for(i=0;i<B_SZ;i++){
    for(j=0;j<B_SZ;j++){
      buf[j]=i!=j?0:(1<<SCALE_BITS);
    }
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
    (*OD_IDCT_1D[B_SZ_LOG-OD_LOG_BSIZE0])(buf,1,buf);
#else
# error "Need an iDCT implementation for this block size."
#endif
    _vp8_scale[i]=0;
    for(j=0;j<B_SZ;j++){
      double c=((double)buf[j])/(1<<SCALE_BITS);
      _vp8_scale[i]+=c*c;
    }
#if PRINT_SCALE
    printf("%s%G",i==0?"":" ",_vp8_scale[i]);
#endif
  }
#if PRINT_SCALE
  printf("\n");
#endif
}

void od_scale_init(double _od_scale[B_SZ]){
  int i;
  int j;
  od_coeff buf[2*B_SZ];
  for(i=0;i<B_SZ;i++){
    for(j=0;j<2*B_SZ;j++){
      buf[j]=(B_SZ>>1)+i!=j?0:(1<<SCALE_BITS);
    }
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
    (*OD_IDCT_1D[B_SZ_LOG-OD_LOG_BSIZE0])(&buf[B_SZ>>1],1,&buf[B_SZ>>1]);
#else
# error "Need an iDCT implementation for this block size."
#endif
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
    (*OD_POST_FILTER[B_SZ_LOG-OD_LOG_BSIZE0])(buf,buf);
    (*OD_POST_FILTER[B_SZ_LOG-OD_LOG_BSIZE0])(&buf[B_SZ],&buf[B_SZ]);
#else
# error "Need a postfilter implementation for this block size."
#endif
    _od_scale[i]=0;
    for(j=0;j<2*B_SZ;j++){
      double c=((double)buf[j])/(1<<SCALE_BITS);
      _od_scale[i]+=c*c;
    }
#if PRINT_SCALE
    printf("%s%G",i==0?"":" ",_od_scale[i]);
#endif
  }
#if PRINT_SCALE
  printf("\n");
#endif
}

#define APPLY_PREFILTER (1)
#define APPLY_POSTFILTER (1)

static void od_prefilter(od_coeff *_out,int _out_stride,od_coeff *_in,
 int _in_stride,int _bx,int _by){
  int bx;
  int by;
  int j;
  int i;
  for(by=0;by<_by;by++){
    int y;
    y=B_SZ*by;
    for(bx=0;bx<_bx;bx++){
      int x;
      x=B_SZ*bx;
      for(j=0;j<B_SZ;j++){
        od_coeff col[B_SZ];
#if APPLY_PREFILTER
        for(i=0;i<B_SZ;i++){
          col[i]=_in[_in_stride*(y+i)+x+j];
        }
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
        (*OD_PRE_FILTER[B_SZ_LOG-OD_LOG_BSIZE0])(col,col);
#else
# error "Need a prefilter implementation for this block size."
#endif
        for(i=0;i<B_SZ;i++){
          _out[_out_stride*(y+i)+x+j]=col[i];
        }
#else
        for(i=0;i<B_SZ;i++){
          _out[_out_stride*(y+i)+x+j]=_in[_in_stride*(y+i)+x+j];
        }
#endif
      }
#if APPLY_PREFILTER
      for(j=0;j<B_SZ;j++){
        od_coeff *row;
        row=&_out[_out_stride*(y+j)+x];
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
        (*OD_PRE_FILTER[B_SZ_LOG-OD_LOG_BSIZE0])(row,row);
#else
# error "Need a prefilter implementation for this block size."
#endif
      }
#endif
    }
  }
}

static void od_postfilter(od_coeff *_out,int _out_stride,od_coeff *_in,
 int _in_stride,int _bx,int _by){
  int bx;
  int by;
  int j;
  int i;
  for(by=0;by<_by;by++){
    int y;
    y=B_SZ*by;
    for(bx=0;bx<_bx;bx++){
      int x;
      x=B_SZ*bx;
      for(j=0;j<B_SZ;j++){
        od_coeff *row;
        for(i=0;i<B_SZ;i++){
          _out[_out_stride*(y+j)+x+i]=_in[_in_stride*(y+j)+x+i];
        }
#if APPLY_POSTFILTER
        row=&_out[_out_stride*(y+j)+x];
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
        (*OD_POST_FILTER[B_SZ_LOG-OD_LOG_BSIZE0])(row,row);
#else
# error "Need a postfilter implementation for this block size."
#endif
#endif
      }
#if APPLY_POSTFILTER
      for(i=0;i<B_SZ;i++){
        od_coeff col[B_SZ];
        for(j=0;j<B_SZ;j++){
          col[j]=_out[_out_stride*(y+j)+x+i];
        }
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
        (*OD_POST_FILTER[B_SZ_LOG-OD_LOG_BSIZE0])(col,col);
#else
# error "Need a postfilter implementation for this block size."
#endif
        for(j=0;j<B_SZ;j++){
          _out[_out_stride*(j+y)+x+i]=col[j];
        }
      }
    }
  }
#endif
}

static void od_fdct_blocks(od_coeff *_out,int _out_stride,od_coeff *_in,
 int _in_stride,int _bx,int _by){
  int bx;
  int by;
  for(by=0;by<_by;by++){
    int y;
    y=B_SZ*by;
    for(bx=0;bx<_bx;bx++){
      int x;
      x=B_SZ*bx;
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
      (*OD_FDCT_2D[B_SZ_LOG-OD_LOG_BSIZE0])(&_out[_out_stride*y+x],_out_stride,
       &_in[_in_stride*y+x],_in_stride);
#else
# error "Need an fDCT implementation for this block size."
#endif
    }
  }
}

static void od_idct_blocks(od_coeff *_out,int _out_stride,od_coeff *_in,
 int _in_stride,int _bx,int _by){
  int bx;
  int by;
  for(by=0;by<_by;by++){
    int y;
    y=B_SZ*by;
    for(bx=0;bx<_bx;bx++){
      int x;
      x=B_SZ*bx;
#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
      (*OD_IDCT_2D[B_SZ_LOG-OD_LOG_BSIZE0])(&_out[_out_stride*y+x],_out_stride,
       &_in[_in_stride*y+x],_in_stride);
#else
# error "Need an iDCT implementation for this block size."
#endif
    }
  }
}

#define SCALE_SATD (1)

/* find the best vp8 mode */
int vp8_select_mode(const unsigned char *_data,int _stride,int *_next_best){
  double best_satd;
  double next_best_satd;
  int    mode;
  int    best_mode;
  int    next_best_mode;
  best_mode=0;
  best_satd=UINT_MAX;
  next_best_mode=best_mode;
  next_best_satd=best_satd;
  for(mode=0;mode<OD_INTRA_NMODES;mode++){
    unsigned char block[B_SZ*B_SZ];
    od_coeff      buf[B_SZ*B_SZ];
    int           j;
    int           i;
    double        satd;

    memset(block,0,B_SZ*B_SZ);
    vp8_intra_predict(block,B_SZ,_data,_stride,mode);
    for(j=0;j<B_SZ;j++){
      for(i=0;i<B_SZ;i++){
        buf[B_SZ*j+i]=block[B_SZ*j+i]-_data[_stride*j+i];
      }
    }

#if B_SZ_LOG>=OD_LOG_BSIZE0&&B_SZ_LOG<OD_LOG_BSIZE0+OD_NBSIZES
    (*OD_FDCT_2D[B_SZ_LOG-OD_LOG_BSIZE0])(buf,B_SZ,buf,B_SZ);
#else
# error "Need an fDCT implementation for this block size."
#endif

    satd=0;
    for(j=0;j<B_SZ;j++){
      for(i=0;i<B_SZ;i++){
#if SCALE_SATD
        satd+=sqrt(VP8_SCALE[j]*VP8_SCALE[i])*abs(buf[B_SZ*j+i]);
#else
        satd+=abs(buf[B_SZ*j+i]);
#endif
      }
    }
    if(satd<best_satd){
      next_best_satd=best_satd;
      next_best_mode=best_mode;
      best_satd=satd;
      best_mode=mode;
    }
    else{
      if(satd<next_best_satd){
        next_best_satd=satd;
        next_best_mode=mode;
      }
    }
  }

  if(_next_best!=NULL){
    *_next_best=next_best_mode;
  }
  return best_mode;
}

od_rgba16_pixel COLORS[OD_INTRA_NMODES];

void image_draw_block(od_rgba16_image *_image,int _x,int _y,
 const unsigned char *_block,int _stride){
  od_rgba16_pixel color;
  int             i;
  int             j;
  color[3]=(unsigned short)0xFFFFU;
  for(i=0;i<B_SZ;i++){
    for(j=0;j<B_SZ;j++){
      color[0]=color[1]=color[2]=_block[_stride*i+j]<<8;
      od_rgba16_image_draw_point(_image,_x+j,_y+i,color);
    }
  }
}

int image_write_png(od_rgba16_image *_image,const char *_name){
  char  fout_name[8192];
  FILE *fout;
  sprintf(fout_name,"%s.png",_name);
  fout=fopen(fout_name,"wb");
  if(fout==NULL){
    fprintf(stderr,"Could not open '%s' for reading.\n",fout_name);
    return EXIT_FAILURE;
  }
  od_rgba16_image_write_png(_image,fout);
  fclose(fout);
  return EXIT_SUCCESS;
}

void image_files_init(image_files *_this,int _nxblocks,int _nyblocks){
  od_rgba16_image_init(&_this->raw,B_SZ*_nxblocks,B_SZ*_nyblocks);
  od_rgba16_image_init(&_this->map,_nxblocks,_nyblocks);
  od_rgba16_image_init(&_this->pred,B_SZ*_nxblocks,B_SZ*_nyblocks);
  od_rgba16_image_init(&_this->res,B_SZ*_nxblocks,B_SZ*_nyblocks);
}

void image_files_clear(image_files *_this){
  od_rgba16_image_clear(&_this->raw);
  od_rgba16_image_clear(&_this->map);
  od_rgba16_image_clear(&_this->pred);
  od_rgba16_image_clear(&_this->res);
}

void image_files_write(image_files *_this,const char *_name,const char *_suf){
  char name[8192];
  sprintf(name,"%s-raw%s",_name,_suf==NULL?"":_suf);
  image_write_png(&_this->raw,name);
  sprintf(name,"%s-map%s",_name,_suf==NULL?"":_suf);
  image_write_png(&_this->map,name);
  sprintf(name,"%s-pred%s",_name,_suf==NULL?"":_suf);
  image_write_png(&_this->pred,name);
  sprintf(name,"%s-res%s",_name,_suf==NULL?"":_suf);
  image_write_png(&_this->res,name);
}

void image_data_init(image_data *_this,const char *_name,int _nxblocks,
 int _nyblocks){
  int w;
  int h;
  _this->name=_name;
  _this->nxblocks=_nxblocks;
  _this->nyblocks=_nyblocks;
  _this->mode=(unsigned char *)malloc(sizeof(*_this->mode)*_nxblocks*_nyblocks);
  w=B_SZ*(_nxblocks+3);
  h=B_SZ*(_nyblocks+3);
  _this->pre=(od_coeff *)malloc(sizeof(*_this->pre)*w*h);
  _this->pre_stride=w;
  w=B_SZ*(_nxblocks+2);
  h=B_SZ*(_nyblocks+2);
  _this->fdct=(od_coeff *)malloc(sizeof(*_this->fdct)*w*h);
  _this->fdct_stride=w;
  w=B_SZ*_nxblocks;
  h=B_SZ*_nyblocks;
  _this->pred=(od_coeff *)malloc(sizeof(*_this->pred)*w*h);
  _this->pred_stride=w;
  w=B_SZ*(_nxblocks+2);
  h=B_SZ*(_nyblocks+2);
  _this->idct=(od_coeff *)malloc(sizeof(*_this->idct)*w*h);
  _this->idct_stride=w;
  w=B_SZ*(_nxblocks+1);
  h=B_SZ*(_nyblocks+1);
  _this->post=(od_coeff *)malloc(sizeof(*_this->post)*w*h);
  _this->post_stride=w;
}

void image_data_clear(image_data *_this){
  free(_this->mode);
  free(_this->pre);
  free(_this->fdct);
  free(_this->pred);
  free(_this->idct);
  free(_this->post);
}

void image_data_pre_block(image_data *_this,const unsigned char *_data,
 int _stride,int _bi,int _bj){
  int      x0;
  int      y0;
  int      bx;
  int      by;
  int      x;
  int      y;
  int      bi;
  int      bj;
  int      i;
  int      j;
  od_coeff buf[B_SZ*B_SZ];
  x0=-(B_SZ>>1);
  y0=-(B_SZ>>1);
  bx=by=1;
  if(_bi==0){
    x0-=B_SZ;
    bx++;
  }
  if(_bj==0){
    y0-=B_SZ;
    by++;
  }
  if(_bi==_this->nxblocks-1){
    bx+=2;
  }
  if(_bj==_this->nyblocks-1){
    by+=2;
  }
  x=x0+B_SZ*_bi+(3*B_SZ>>1);
  y=y0+B_SZ*_bj+(3*B_SZ>>1);
  for(bj=0;bj<by;bj++){
    for(bi=0;bi<bx;bi++){
      for(j=0;j<B_SZ;j++){
        for(i=0;i<B_SZ;i++){
          buf[B_SZ*j+i]=
           (_data[_stride*(y0+B_SZ*bj+j)+x0+B_SZ*bi+i]-128)*INPUT_SCALE;
        }
      }
      od_prefilter(&_this->pre[_this->pre_stride*(y+B_SZ*bj)+x+B_SZ*bi],
       _this->pre_stride,buf,B_SZ,1,1);
    }
  }
}

void image_data_fdct_block(image_data *_this,int _bi,int _bj){
  int x0;
  int y0;
  int bx;
  int by;
  int x;
  int y;
  x0=_bi*B_SZ+(3*B_SZ>>1);
  y0=_bj*B_SZ+(3*B_SZ>>1);
  bx=by=1;
  if(_bi==0){
    x0-=B_SZ;
    bx++;
  }
  if(_bj==0){
    y0-=B_SZ;
    by++;
  }
  if(_bi==_this->nxblocks-1){
    bx++;
  }
  if(_bj==_this->nyblocks-1){
    by++;
  }
  x=x0-(B_SZ>>1);
  y=y0-(B_SZ>>1);
  od_fdct_blocks(&_this->fdct[y*_this->fdct_stride+x],_this->fdct_stride,
   &_this->pre[y0*_this->pre_stride+x0],_this->pre_stride,bx,by);
}

void image_data_mode_block(image_data *_this,int _bi,int _bj){
  od_coeff *block;
  int       best_mode;
  double    best_satd;
  int       mode;
  block=&_this->fdct[_this->fdct_stride*B_SZ*(_bj+1)+B_SZ*(_bi+1)];
  best_mode=0;
  best_satd=UINT_MAX;
  for(mode=0;mode<OD_INTRA_NMODES;mode++){
    double p[B_SZ*B_SZ];
    double satd;
    int    i;
    int    j;
#if 0
    od_intra_pred4x4_mult(block,_this->fdct_stride,mode,p);
#else
    ne_intra_pred4x4_mult(block,_this->fdct_stride,mode,p);
#endif
    satd=0;
    for(j=0;j<B_SZ;j++){
      for(i=0;i<B_SZ;i++){
#if SCALE_SATD
        satd+=sqrt(OD_SCALE[j]*OD_SCALE[i])*
         abs(block[_this->fdct_stride*j+i]-(od_coeff)floor(p[B_SZ*j+i]+0.5));
#else
        satd+=
         abs(block[_this->fdct_stride*j+i]-(od_coeff)floor(p[B_SZ*j+i]+0.5));
#endif
      }
    }
    if(satd<best_satd){
      best_mode=mode;
      best_satd=satd;
    }
  }
  _this->mode[_bj*_this->nxblocks+_bi]=best_mode;
}

void image_data_pred_block(image_data *_this,int _bi,int _bj){
  int       mode;
  od_coeff *block;
  double    p[B_SZ*B_SZ];
  od_coeff *pred;
  int       j;
  int       i;
  mode=_this->mode[_this->nxblocks*_bj+_bi];
  block=&_this->fdct[_this->fdct_stride*B_SZ*(_bj+1)+B_SZ*(_bi+1)];
#if 0
  od_intra_pred4x4_mult(block,_this->fdct_stride,mode,p);
#else
  ne_intra_pred4x4_mult(block,_this->fdct_stride,mode,p);
#endif
  pred=&_this->pred[_this->pred_stride*B_SZ*_bj+B_SZ*_bi];
  for(j=0;j<B_SZ;j++){
    for(i=0;i<B_SZ;i++){
      pred[_this->pred_stride*j+i]=(od_coeff)floor(p[B_SZ*j+i]+0.5);
    }
  }
}

void image_data_stats_block(image_data *_this,const unsigned char *_data,
 int _stride,int _bi,int _bj,intra_stats *_stats){
  int        mode;
  mode_data *fr;
  mode_data *md;
  od_coeff  *pred;
  od_coeff  *block;
  int        j;
  int        i;
  od_coeff   buf[B_SZ*B_SZ];

  mode=_this->mode[_bj*_this->nxblocks+_bi];

  fr=&_stats->fr;
  md=&_stats->md[mode];

  fr->n++;
  md->n++;

  /* update the input mean and variance */
  mode_data_add_input(fr,_data,_stride);
  mode_data_add_input(md,_data,_stride);

  /* update the daala reference mean and covariance */
  block=&_this->fdct[_this->fdct_stride*B_SZ*(_bj+1)+B_SZ*(_bi+1)];

  mode_data_add_block(fr,block,_this->fdct_stride,1);
  mode_data_add_block(md,block,_this->fdct_stride,1);

  /* update the daala predicted mean and covariance */
  pred=&_this->pred[_this->pred_stride*B_SZ*_bj+B_SZ*_bi];

  for(j=0;j<B_SZ;j++){
    for(i=0;i<B_SZ;i++){
      buf[B_SZ*j+i]=block[_this->fdct_stride*j+i]-pred[_this->pred_stride*j+i];
    }
  }

  mode_data_add_block(fr,buf,B_SZ,0);
  mode_data_add_block(md,buf,B_SZ,0);

  /* update the daala satd */
  for(j=0;j<B_SZ;j++){
    for(i=0;i<B_SZ;i++){
      double satd;
      satd=abs(buf[B_SZ*j+i]);
      md->satd_avg[B_SZ*j+i]+=(satd-md->satd_avg[B_SZ*j+i])/md->n;
      fr->satd_avg[B_SZ*j+i]+=(satd-fr->satd_avg[B_SZ*j+i])/fr->n;
    }
  }
}

void image_data_idct_block(image_data *_this,int _bi,int _bj){
  int x0;
  int y0;
  int x;
  int y;
  int bx;
  int by;
  x0=B_SZ*_bi;
  y0=B_SZ*_bj;
  x=x0+B_SZ;
  y=y0+B_SZ;
  bx=by=1;
  if(_bi==0){
    x-=B_SZ;
    bx++;
  }
  if(_bj==0){
    y-=B_SZ;
    by++;
  }
  if(_bi==_this->nxblocks-1){
    bx++;
  }
  if(_bj==_this->nyblocks-1){
    by++;
  }
  /* TODO remove redundant computations here */
  if(bx!=1||by!=1){
    od_idct_blocks(&_this->idct[_this->idct_stride*y+x],_this->idct_stride,
     &_this->fdct[_this->fdct_stride*y+x],_this->fdct_stride,bx,by);
  }
  x=x0+B_SZ;
  y=y0+B_SZ;
  od_idct_blocks(&_this->idct[_this->idct_stride*y+x],_this->idct_stride,
   &_this->pred[_this->pred_stride*y0+x0],_this->pred_stride,1,1);
}

void image_data_post_block(image_data *_this,int _bi,int _bj){
  int x0;
  int y0;
  int x;
  int y;
  int bx;
  int by;
  x=B_SZ*_bi;
  y=B_SZ*_bj;
  x0=x+(B_SZ>>1);
  y0=y+(B_SZ>>1);
  bx=by=1;
  if(_bi==_this->nxblocks-1){
    bx++;
  }
  if(_bj==_this->nyblocks-1){
    by++;
  }
  od_postfilter(&_this->post[_this->post_stride*y+x],_this->post_stride,
   &_this->idct[_this->idct_stride*y0+x0],_this->idct_stride,bx,by);
}

void image_data_files_block(image_data *_this,const unsigned char *_data,
 int _stride,int _bi,int _bj,image_files *_files){
  int            mode;
  od_coeff      *p;
  int            j;
  int            i;
  od_coeff       v;
  unsigned char  buf[B_SZ*B_SZ];

  mode=_this->mode[_bj*_this->nxblocks+_bi];

  od_rgba16_image_draw_point(&_files->map,_bi,_bj,COLORS[mode]);

  p=&_this->pre[_this->pre_stride*(B_SZ*_bj+(3*B_SZ>>1))+B_SZ*_bi+(3*B_SZ>>1)];
  for(j=0;j<B_SZ;j++){
    for(i=0;i<B_SZ;i++){
      v=(p[_this->pre_stride*j+i]+INPUT_SCALE*128+INPUT_SCALE/2)/INPUT_SCALE;
      buf[B_SZ*j+i]=OD_CLAMPI(0,v,255);
    }
  }
  image_draw_block(&_files->raw,B_SZ*_bi,B_SZ*_bj,buf,B_SZ);

  p=&_this->post[_this->post_stride*(B_SZ*_bj+(B_SZ>>1))+B_SZ*_bi+(B_SZ>>1)];
  for(j=0;j<B_SZ;j++){
    for(i=0;i<B_SZ;i++){
      v=(p[_this->post_stride*j+i]+INPUT_SCALE*128+INPUT_SCALE/2)/INPUT_SCALE;
      buf[B_SZ*j+i]=OD_CLAMPI(0,v,255);
    }
  }
  image_draw_block(&_files->pred,B_SZ*_bi,B_SZ*_bj,buf,B_SZ);

  for(j=0;j<B_SZ;j++){
    for(i=0;i<B_SZ;i++){
      buf[B_SZ*j+i]=abs(_data[_stride*j+i]-buf[B_SZ*j+i]);
    }
  }
  image_draw_block(&_files->res,B_SZ*_bi,B_SZ*_bj,buf,B_SZ);
}

int image_data_save_map(image_data *_this){
  char  name[8196];
  int   eos;
  FILE *fout;
  strcpy(name,_this->name);
  eos=strlen(name)-4;
  sprintf(&name[eos],".map");
  fout=fopen(name,"wb");
  if(fout==NULL){
    fprintf(stderr,"Error opening output file '%s'.\n",name);
    return EXIT_FAILURE;
  }
  if(fwrite(_this->mode,_this->nxblocks*(size_t)_this->nyblocks,1,fout)<1){
    fprintf(stderr,"Error writing to output file '%s'.\n",name);
    return EXIT_FAILURE;
  }
  fclose(fout);
  return EXIT_SUCCESS;
}

int image_data_load_map(image_data *_this){
  char  name[8196];
  int   eos;
  FILE *fin;
  strcpy(name,_this->name);
  eos=strlen(name)-4;
  sprintf(&name[eos],".map");
  fin=fopen(name,"rb");
  if(fin==NULL){
    fprintf(stderr,"Error opening input file '%s'.\n",name);
    return EXIT_FAILURE;
  }
  if(fread(_this->mode,_this->nxblocks*(size_t)_this->nyblocks,1,fin)<1) {
    fprintf(stderr,"Error reading from input file '%s'.\n",name);
    return EXIT_FAILURE;
  }
  fclose(fin);
  return EXIT_SUCCESS;
}

double NE_PRED_OFFSETS_4x4[OD_INTRA_NMODES][4][4];
double NE_PRED_WEIGHTS_4x4[OD_INTRA_NMODES][4][4][5][4][4];

void ne_intra_pred4x4_mult(const od_coeff *_c,int _stride,int _mode,double *_p){
  int j;
  int i;
  int by;
  int bx;
  int k;
  int l;
  for(j=0;j<4;j++){
    for(i=0;i<4;i++){
      _p[4*j+i]=NE_PRED_OFFSETS_4x4[_mode][j][i];
      for(by=0;by<=1;by++){
        for(bx=0;bx<=2;bx++){
	  const od_coeff *b;
          if(by==1&&bx==2){
            break;
          }
	  b=&_c[_stride*4*(by-1)+4*(bx-1)];
          for(k=0;k<4;k++){
            for(l=0;l<4;l++){
              _p[4*j+i]+=
               b[_stride*k+l]*NE_PRED_WEIGHTS_4x4[_mode][j][i][3*by+bx][k][l];
            }
          }
        }
      }
    }
  }
}

void print_betas(){
  int m;
  int i;
  int j;
  int k;
  int l;
  int bx;
  int by;
  for(m=0;m<OD_INTRA_NMODES;m++){
    fprintf(stderr,"/* Mode %i */\n{\n",m);
    for(j=0;j<B_SZ;j++){
      for(i=0;i<B_SZ;i++){
        fprintf(stderr,"%- 24.18G",NE_PRED_OFFSETS_4x4[m][j][i]);
      }
    }
    fprintf(stderr,"}\n");
    for(j=0;j<B_SZ;j++){
      for(i=0;i<B_SZ;i++){
        fprintf(stderr,"/* Mode %i (%i,%i) */\n{\n",m,i,j);
        for(by=0;by<=1;by++){
          for(k=0;k<B_SZ;k++){
            fprintf(stderr,"  {");
            for(bx=0;bx<=2;bx++){
              if(by==1&&bx==2){
                break;
              }
              for(l=0;l<B_SZ;l++){
                fprintf(stderr,"%- 24.18G",NE_PRED_WEIGHTS_4x4[j][i][m][3*by+bx][k][l]);
              }
              fprintf(stderr,"   ");
            }
            fprintf(stderr,"}\n");
          }
        }
      }
    }
    fprintf(stderr,"\n");
  }
}
