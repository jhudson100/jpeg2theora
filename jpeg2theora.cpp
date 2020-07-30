
// This is a simple tool to convert a series of jpeg files
// to an Ogg Theora video.
// Usage:
//  jpeg2theora framesPerSec outputfile inputpattern
// Example:
//  jpeg2theora 25 movie.ogv pic%04d.jpg

//  g++ -g jpeg2theora.cpp -o jpeg2theora -logg  -ltheora -ltheoraenc -ltheoradec -lturbojpeg

//~ Copyright (c) 2020 J Hudson

//~ Permission is hereby granted, free of charge, to any person obtaining a copy
//~ of this software and associated documentation files (the "Software"), to deal
//~ in the Software without restriction, including without limitation the rights
//~ to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//~ copies of the Software, and to permit persons to whom the Software is
//~ furnished to do so, subject to the following conditions:

//~ The above copyright notice and this permission notice shall be included in all
//~ copies or substantial portions of the Software.

//~ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//~ IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//~ FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//~ AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//~ LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//~ OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//~ SOFTWARE.


#include <theora/theoraenc.h>
#include <cassert>
#include <string>
#include <fstream>
#include <turbojpeg.h>
#include <vector>
#include <iostream>
#include <cstring>


th_comment comments;
th_enc_ctx* ctx;
ogg_stream_state os;
ogg_page og;
ogg_packet op;
std::ofstream out;

void flush();
void pageoutLoop();
void outputHeader1();
void outputHeader2();
 
int main(int argc, char* argv[])
{
    int fps = atoi(argv[1]);
    std::string outfile = argv[2];
    std::string pattern = argv[3];  //sprintf style pattern
    
    out.open(outfile,std::ios::binary);
    if(!out.good() )
        throw std::runtime_error("Cannot open output file "+outfile);
        
    //storage space for color channels
    std::vector<unsigned char> Y,Cb,Cr;

    //strides in Y, Cb, and Cr
    int strides[3];

    //filename buffer: Output of sprintf
    std::vector<char> pstr(pattern.length()+32);
    
    //handle for turbojpeg decompressor
    tjhandle jpegD = tjInitDecompress();
    
    //destination planes for JPEG decompress
    unsigned char* dplanes[3];
    
    //theora info structure
    th_info tinfo;

    //buffer for theora compression.
    th_ycbcr_buffer buff;
    
    //data from jpeg file
    std::vector<char> fdat;
    
    //frame counter
    int i=0;
    
    //size, subsampling, and color space of jpeg file
    int w=0,h,subs,cspace;
    
    while(1){
        
        if( snprintf(pstr.data(),pstr.size(),pattern.c_str(),i) >= pstr.size() )
            throw std::runtime_error("Pattern was too long");
            
        i++;
        
        //read the jpeg file from disk
        
        std::ifstream in(pstr.data(),std::ios::binary);
        if(!in.good()){
            if( i == 1 ){
                //maybe we didn't have a file with 0
                //as its number. go again and try 1 as the first file number
                continue;
            } else{
                //at end. stop.
                break;
            }
        }
        
        //get size of file and read it in
        in.seekg(0,std::ios::end);
        fdat.resize(in.tellg());
        in.seekg(0);
        in.read(fdat.data(), fdat.size());

        //check file and make sure size matches
        //the other ones decompressed so far.
        //If this is the first file,
        //initialize theora
        int w1,h1,subs1,cspace1;
        tjDecompressHeader3(jpegD,
            (unsigned char*)fdat.data(), fdat.size(),
            &w1, &h1, &subs1,&cspace1);
             
        if( w == 0 ){
            //first frame
            w=w1;
            h=h1;
            subs=subs1;
            cspace=cspace1;
            
            int framewidth = (w+15)&~0xf;
            int frameheight = (h+15)&~0xf;
            th_pixel_fmt thpixf;
            
            switch(subs){
                case TJSAMP_444:
                    thpixf=TH_PF_444;
                    break;
                case TJSAMP_422: 
                    thpixf=TH_PF_422;
                    break;
                case TJSAMP_420: 
                    thpixf=TH_PF_420;
                    break;
                default:
                    throw std::runtime_error("Unsupported subsampling: "+std::to_string(subs));
            }
            int cbframew = tjPlaneWidth(1,framewidth,subs);
            int cbframeh = tjPlaneHeight(1,frameheight,subs);
            int cbpicw = tjPlaneWidth(1,w,subs);
            int cbpich = tjPlaneHeight(1,h,subs);
            
            Y.resize(tjPlaneSizeYUV(0,framewidth,framewidth,frameheight,subs));
            Cb.resize(tjPlaneSizeYUV(1,framewidth,framewidth,frameheight,subs));
            Cr.resize(tjPlaneSizeYUV(2,framewidth,framewidth,frameheight,subs));
            dplanes[0] = (unsigned char*)Y.data();
            dplanes[1] = (unsigned char*)Cb.data();
            dplanes[2] = (unsigned char*)Cr.data();
            strides[0] = framewidth;
            strides[1] = cbframew;
            strides[2] = cbframew;
                    
            buff[0].width = framewidth;
            buff[0].height = frameheight;
            buff[0].stride = framewidth;
            buff[0].data = Y.data();
            
            buff[1].width = cbframew;
            buff[1].height = cbframeh;
            buff[1].stride = cbframew;
            buff[1].data = Cb.data();
            
            buff[2].width = buff[1].width;
            buff[2].height = buff[1].height;
            buff[2].stride = buff[1].stride;
            buff[2].data = Cr.data();
            
            th_comment_init(&comments);
            th_info_init(&tinfo);
            tinfo.frame_width = framewidth;
            tinfo.frame_height = frameheight;
            tinfo.pic_width = w;
            tinfo.pic_height = h;
            tinfo.pic_x=0;
            tinfo.pic_y=0;
            tinfo.fps_numerator = fps;
            tinfo.fps_denominator = 1;
            tinfo.colorspace = TH_CS_ITU_REC_470M;
            tinfo.pixel_fmt = thpixf;
            tinfo.target_bitrate = 0;
            tinfo.quality = 48;
            ctx = th_encode_alloc(&tinfo);
            if(!ctx)
                throw std::runtime_error("th encode alloc");
                
            outputHeader1();
            //if we had an audio stream,
            //we'd need to output its initial header packet
            //before calling outputHeader2
            outputHeader2();
            
        } else {
            if( w!=w1 )
                throw std::runtime_error("width mismatch");
            if( h!=h1 )
                throw std::runtime_error("height mismatch");
            if( subs!=subs1 )
                throw std::runtime_error("subsample mismatch");
            if( cspace!=cspace1 )
                throw std::runtime_error("colorspace mismatch");
        }
        
        tjDecompressToYUVPlanes( jpegD,
            (unsigned char*)fdat.data(),
            fdat.size(),
            dplanes,
            w,
            strides,
            h, 0 );

        th_encode_ycbcr_in( ctx, buff );

        while(true){
            int rv = th_encode_packetout( ctx, false, &op );
            if( rv < 0 )
                throw std::runtime_error("packetout");
            if( rv == 0 ){
                break;
            }
            if( ogg_stream_packetin( &os, &op ) < 0 )
                throw std::runtime_error("packetin");
            pageoutLoop(); 
        }
        
        std::cout << i << " ";
        std::cout.flush();
    }
    
    std::cout << "\n";
    
    //end of stream. we duplicate
    //whatever the last frame was
    //that was encoded. That seems a bit icky,
    //but it should be OK visually.
    th_encode_ycbcr_in( ctx, buff );
    while(true){
        int rv = th_encode_packetout( ctx, true, &op );
        if( rv < 0 )
            throw std::runtime_error("packetout");
        if( rv == 0 ){
            break;
        }
        if( ogg_stream_packetin( &os, &op ) < 0 )
            throw std::runtime_error("packetin");
        pageoutLoop(); 
    }

    while(true){
        int rv = ogg_stream_flush(&os, &og);
        if( rv == 0 )
            break;
        pageoutLoop(); 
    }

    th_encode_free(ctx);
    tjDestroy(jpegD);
    th_comment_clear(&comments);

    return 0;
}


//output initial header packet
void outputHeader1(){
    ogg_stream_init(&os,1);
    th_encode_flushheader(ctx,&comments,&op);
    ogg_stream_packetin( &os, &op );
    flush();
}


//output last 2 header packets
void outputHeader2(){
    while(true){
        int rv = th_encode_flushheader(ctx,&comments,&op);
        if( rv == 0 )
            break;
        if(rv<0)
            throw std::runtime_error("Error");
        ogg_stream_packetin( &os, &op );
    }
    flush();
}


//write page to output, if there is one
void pageoutLoop()
{
    while(true){
        int rv = ogg_stream_pageout( &os, &og );
        if( rv == 0 )
            return;
        out.write( (char*) og.header, og.header_len );
        out.write( (char*) og.body, og.body_len );
    }
}

//flush stream, outputting all buffered data
void flush(){
    while(true){
        int rv = ogg_stream_flush(&os,&og);
        if( rv == 0 )
            break;
        out.write( (char*) og.header, og.header_len );
        out.write( (char*) og.body, og.body_len );
    }
}


