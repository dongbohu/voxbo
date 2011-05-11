
// ff_dicom4d.cpp
// VoxBo I/O plug-in for DICOM format, with siemens extensions
// Copyright (c) 2003 by The VoxBo Development Team

// VoxBo is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// VoxBo is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with VoxBo.  If not, see <http://www.gnu.org/licenses/>.
// 
// For general information on VoxBo, including the latest complete
// source code and binary distributions, manual, and associated files,
// see the VoxBo home page at: http://www.voxbo.org/
//
// original version written by Dan Kimberg

using namespace std;

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sstream>
#include "vbutil.h"
#include "vbio.h"

extern "C" {

#include "dicom.h"

vf_status test_dcm4d_4D(char *buf,int bufsize,string filename);
int read_head_dcm4d_4D(Tes *mytes);
int read_data_dcm4d_4D(Tes *mytes);

#ifdef VBFF_PLUGIN
VBFF vbff()
#else
VBFF dcm4d_vbff()
#endif
{
  VBFF tmp;
  tmp.name="DICOM 4D";
  tmp.extension="dcm";
  tmp.signature="dcm4d";
  tmp.dimensions=4;
  tmp.version_major=vbversion_major;
  tmp.version_minor=vbversion_minor;
  tmp.test_4D=test_dcm4d_4D;
  tmp.read_head_4D=read_head_dcm4d_4D;
  tmp.read_data_4D=read_data_dcm4d_4D;
  return tmp;
}

#define SIEMENS_TAG "### ASCCONV BEGIN ###"
#define SIEMENS_TAG_END "### ASCCONV END ###"

int read_head_dcm3d_3D(Cube *cb);
int read_data_dcm3d_3D(Cube *cb);
int read_multiple_slices(Cube *cb,string pat);
int read_multiple_slices_from_files(Cube *cb,vector<string>filenames);

vf_status
test_dcm4d_4D(char *buf,int bufsize,string filename)
{
  // struct stat st;
  glob_t gb;
  string pat=patfromname(filename);
  // if the file exists but it's too short, go home
  if (pat==filename && bufsize<200)
    return vf_no;
  // no match, go home
  if (glob(pat.c_str(),0,NULL,&gb))
    return vf_no;
  dicominfo dci;
  // bad dicom header, go home
  if (read_dicom_header(gb.gl_pathv[0],dci)) {
    globfree(&gb);
    return vf_no;
  }
  int cnt=gb.gl_pathc;
  globfree(&gb);
  // if we're not mosaiced and we don't have more than one full volume
  if (!dci.mosaicflag && cnt<=dci.dimz)
    return vf_no;
  // if we're not mosaiced and we don't have an integer number of volumes
  if (!dci.mosaicflag && (cnt % dci.dimz)!=0)
    return vf_no;
  // okay, either we're mosaiced or we have more than dimz files
  return vf_yes;
}

int
read_head_dcm4d_4D(Tes *tes)
{
  dicominfo dci;
  stringstream tmps;
  glob_t gb;
  int filecount=0;

  string fname=tes->GetFileName();
  string pat=patfromname(fname);
  if (pat != fname) {
    glob(pat.c_str(),0,NULL,&gb);
    if (gb.gl_pathc<1) {
      globfree(&gb);
      return 120;
    }
    fname=gb.gl_pathv[0];
    filecount=gb.gl_pathc;
    globfree(&gb);
  }

  if (read_dicom_header(fname,dci))
    return 150;

  for (int i=0; i<(int)dci.protocol.size(); i++) {
    if (dci.protocol[i]==' ')
      dci.protocol[i]='_';
  }
  dci.protocol=xstripwhitespace(dci.protocol,"_");
  transfer_dicom_header(dci,*tes);
  tes->dimt=filecount;
  if (!dci.mosaicflag)
    tes->dimt=filecount/dci.dimz;
  return(0);   // no error!
}

// FIXME dicominfo needs a constructor that sets defaults

int
read_data_dcm4d_4D(Tes *tes)
{
  dicominfo dci;
  glob_t gb;

  string fname=tes->GetFileName();
  string pat=patfromname(fname);
  if (glob(pat.c_str(),0,NULL,&gb))
    return 105;

  tokenlist filenames;
  glob_transfer(filenames,gb);
  globfree(&gb);

  if (filenames.size()<1)
    return 110;
  
  if (read_dicom_header(filenames[0],dci))
    return 150;

  // here is where we handle un-mosaiced 4D time series
  if (!dci.mosaicflag) {
    int timepoints=filenames.size()/dci.dimz;
    Cube cb;
    transfer_dicom_header(dci,*tes);
    tes->SetVolume(dci.dimx,dci.dimy,dci.dimz,timepoints,vb_short);
    // tes->print();
    if (!tes->data)
      return 121;
    for (int i=0; i<timepoints; i++) {
      vector<string> cnames;
      for (int j=i*dci.dimz; j<(i+1)*dci.dimz; j++)
        cnames.push_back(filenames[j]);
      read_multiple_slices_from_files(&cb,cnames);
      tes->SetCube(i,cb);
    }
    // tes->print();
    tes->data_valid=1;
    return (0);
  }

  for (int i=0; i<filenames.size(); i++) {
    Cube cb;
    cb.SetFileName(filenames[i]);
    if (read_head_dcm3d_3D(&cb))
      continue;
    if (i==0) {
      tes->SetVolume(cb.dimx,cb.dimy,cb.dimz,filenames.size(),cb.datatype);
      if (!tes->data)
        return 120;
      tes->voxsize[0]=cb.voxsize[0];
      tes->voxsize[1]=cb.voxsize[1];
      tes->voxsize[2]=cb.voxsize[2];
      tes->filebyteorder=cb.filebyteorder;
      tes->header=cb.header;
    }
    if (read_data_dcm3d_3D(&cb))
      continue;
    tes->SetCube(i,cb);
  }

  tes->data_valid=1;
  tes->Remask();
  return(0);   // no error!
}

// FIXME below is a copy (!) of the 3D stuff

int
read_head_dcm3d_3D(Cube *cb)
{
  dicominfo dci;
  stringstream tmps;
  glob_t gb;

  string fname=cb->GetFileName();
  string pat=patfromname(fname);
  if (pat != fname) {
    glob(pat.c_str(),0,NULL,&gb);
    if (gb.gl_pathc<1) {
      globfree(&gb);
      return 120;
    }
    fname=gb.gl_pathv[0];
    globfree(&gb);
  }

  if (read_dicom_header(fname,dci))
    return 120;

  for (int i=0; i<(int)dci.protocol.size(); i++) {
    if (dci.protocol[i]==' ')
      dci.protocol[i]='_';
  }
  dci.protocol=xstripwhitespace(dci.protocol,"_");
  transfer_dicom_header(dci,*cb);
  return(0);   // no error!
}

// FIXME dicominfo needs a constructor that sets defaults

int
read_data_dcm3d_3D(Cube *cb)
{
  dicominfo dci;

  string fname=cb->GetFileName();
  string pat=patfromname(fname);
  if (pat != fname) {
    return read_multiple_slices(cb,pat);
  }

  if (read_dicom_header(cb->GetFileName(),dci))
    return 140;
  if (dci.dimx != cb->dimx || dci.dimy != cb->dimy || dci.dimz != cb->dimz)
    return 105;
  
  cb->SetVolume(dci.dimx,dci.dimy,dci.dimz,vb_short);
  if (!cb->data_valid)
    return 120;
  int volumesize=dci.dimx*dci.dimy*dci.dimz*cb->datasize;
  
  // make sure we can get all the slices (there will be blanks)
  if (dci.datasize<volumesize)
    return 130;

  FILE *ifile = fopen(cb->GetFileName().c_str(),"r");
  if (!ifile)
    return 110;
  fseek(ifile,dci.offset,SEEK_SET);
  unsigned char *newdata=new unsigned char[dci.datasize];
  if (!newdata)
    return 160;
  int cnt=fread(newdata,1,dci.datasize,ifile);
  fclose(ifile);
  if (cnt < volumesize) {
    delete [] newdata;
    return 150;
  }

  // de-mosaic if needed
  if (dci.mosaicflag) {
    int xoffset=0;
    int yoffset=0;
    int ind=0;
    for (int k=0; k<cb->dimz; k++) {
      if (xoffset>=dci.cols) {
        xoffset=0;
        yoffset+=dci.dimy;
      }
      // first row for this cube
      int rowstart=((yoffset*dci.cols)+(xoffset))*cb->datasize;
      rowstart+=cb->dimy*cb->datasize*dci.cols;
      for (int j=0; j<cb->dimy; j++) {
        // copy a whole row and position for the next
        memcpy(cb->data+ind,newdata+rowstart,dci.dimx*cb->datasize);
        rowstart-=dci.cols*cb->datasize;
        ind+=dci.dimx*cb->datasize;
      }
      xoffset+=dci.dimx;
    }
  }
  else {
    // FIXME handle non-mosaiced (single slices)
  }
  delete [] newdata;

  // FIXME valid if what?
  if (dci.byteorder!=my_endian())
    cb->byteswap();
  cb->data_valid=1;
  return(0);   // no error!
}

// int
// read_multiple_slices(Cube *cb,string pat)
// {
//   glob_t gb;
//   dicominfo dci;
//   if (glob(pat.c_str(),0,NULL,&gb))
//     return 100;
  
//   tokenlist filenames;
//   glob_transfer(filenames,gb);
//   globfree(&gb);

//   if (read_dicom_header(gb.gl_pathv[0],dci))
//     return 120;
  
//   if (dci.dimx == 0 || dci.dimy == 0 || dci.dimz == 0)
//     return 105;
//   cb->SetVolume(dci.dimx,dci.dimy,dci.dimz,vb_short);
//   if (!cb->data_valid)
//     return 120;
//   int slicesize=dci.dimx*dci.dimy*cb->datasize;

//   unsigned char *newdata=new unsigned char[dci.datasize];
//   if (!newdata)
//     return 150;
//   for (int i=0; i<dci.dimz; i++) {
//     // prematurely out of slices, no complaint i guess
//     if (i>filenames.size()-1)
//       break;
//     dicominfo dci2;
//     if (read_dicom_header(filenames[i],dci2))
//       continue;
//     //cout << "dicomdatasize: " << dci2.datasize << "  slicesize: " << slicesize << endl;
//     // FIXME!
// //     if (dci2.datasize<slicesize)
// //       continue;
//     FILE *ifile = fopen(filenames(i),"r");
//     if (!ifile)
//       continue;
//     fseek(ifile,dci2.offset,SEEK_SET);
//     int cnt=fread(newdata,1,dci2.datasize,ifile);
//     fclose(ifile);
//     if (cnt < dci2.datasize)
//       continue;
//     memcpy(cb->data+(slicesize*i),newdata,slicesize);
//   }
//   if (dci.byteorder!=my_endian())
//     cb->byteswap();
//   return 0;
// }

// FIXME the following is a huge kludge

int
read_multiple_slices_from_files(Cube *cb,vector<string>filenames)
{
  dicominfo dci;

//   cout << "list:" << endl;
//   for (int i=0; i<(int)filenames.size(); i++) 
//     cout << filenames[i] << endl;

  if (read_dicom_header(filenames[0],dci))
    return 120;
  if (dci.dimx == 0 || dci.dimy == 0 || dci.dimz == 0)
    return 105;
  cb->SetVolume(dci.dimx,dci.dimy,dci.dimz,vb_short);
  if (!cb->data_valid)
    return 120;
  int slicesize=dci.dimx*dci.dimy*cb->datasize;

  unsigned char *newdata=new unsigned char[dci.datasize];
  if (!newdata)
    return 150;
  for (int i=0; i<dci.dimz; i++) {
    // prematurely out of slices, no complaint i guess
    if (i>(int)filenames.size()-1)
      break;
    dicominfo dci2;
    if (read_dicom_header(filenames[i],dci2))
      continue;
    //cout << "dicomdatasize: " << dci2.datasize << "  slicesize: " << slicesize << endl;
    // FIXME!
//     if (dci2.datasize<slicesize)
//       continue;
    FILE *ifile = fopen(filenames[i].c_str(),"r");
    if (!ifile)
      continue;
    fseek(ifile,dci2.offset,SEEK_SET);
    int cnt=fread(newdata,1,dci2.datasize,ifile);
    fclose(ifile);
    if (cnt < dci2.datasize)
      continue;
    memcpy(cb->data+(slicesize*i),newdata,slicesize);
  }
  if (dci.byteorder!=my_endian())
    cb->byteswap();
  return 0;
}

} // extern "C"