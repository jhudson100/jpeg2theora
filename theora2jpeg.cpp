

// This is a simple tool to convert an Ogg Theora video
// to a series of jpeg files
// Usage:
//  theora2jpeg inputfile outputpattern [maxframes]
// Examples:
//  theora2jpeg movie.ogv pic%04d.jpg
//  theora2jpeg movie.ogv pic%04d.jpg 10

// Note: Only supports ogg files with a single stream.
// If there's an audio stream, this will likely not work correctly.

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



//reference:
//https://bluishcoder.co.nz/2009/06/24/reading-ogg-files-using-libogg.html

#include <memory>
#include <turbojpeg.h>
#include <theora/theoradec.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cassert>
#include <climits>
#include <vector>
#include <map>

#define DEBUG 0

ogg_stream_state* os;
ogg_sync_state oy;
ogg_page og;
ogg_packet op;
std::ifstream in;
int theoraStream=-1;

bool getPacket();
bool getPage();
bool readFile();
bool getNextFrame(th_dec_ctx* ctx, th_ycbcr_buffer& buff);
    
    
int main(int argc, char* argv[])
{
    std::string infile = argv[1];
    std::string outfileTemplate = argv[2];
    
    unsigned numFrames;
    if( argc > 3 )
        numFrames = std::atoi(argv[3]);
    else
        numFrames = UINT_MAX;

    in.open(infile,std::ios::binary);
    if(!in.good())
        throw std::runtime_error("Cannot open "+infile);
        
    th_dec_ctx* ctx;
    th_info tinfo;
    ogg_int64_t granulepos;
    th_ycbcr_buffer buff;
    int serialNumber=-1;
    tjhandle  jhandle = tjInitCompress();
    unsigned subsamp;

    th_comment comm;
    th_setup_info* setup=nullptr;

    th_info_init(&tinfo);
    th_comment_init(&comm);
        
    int rv = ogg_sync_init(&oy);
    assert(rv==0);


    while(true){
        if( getPacket() == false )
            throw std::runtime_error("Could not get packet for setup for "+infile);
        rv = th_decode_headerin(&tinfo,&comm,&setup, &op );
       
        if( DEBUG )
            std::cerr << "th_decode_headerin: " << rv << "\n";
            
        if( rv == TH_ENOTFORMAT ){
            //this is not a theora header. Maybe vorbis?
            if(DEBUG)
                std::cerr << "Not a theora header. Vorbis?\n";
        } else if( rv < 0 ){ 
            //some other error
            throw std::runtime_error("th_decode_headerin error: "+std::to_string(rv) );
        } else if(rv == 0 ){
            //all header packets processed
            break;
        } else if( rv > 0 ){
            //ok header, but not the last one
            if( theoraStream == -1 ){
                int sno = ogg_page_serialno(&og);
                theoraStream = sno;
                if(DEBUG)
                    std::cerr << "Theora stream: " << sno << "\n";
            }
        }
    }
    
    if( theoraStream == -1 )
        throw std::runtime_error("No theora streams found");
    
    ctx = th_decode_alloc(&tinfo, setup);
    if( !ctx )
        throw std::runtime_error("th_decode_alloc error");
    
    th_setup_free(setup);
    
    switch(tinfo.pixel_fmt){
        case TH_PF_444:
            subsamp = TJSAMP_444; break;
        case TH_PF_422:
            subsamp = TJSAMP_422; break;
        case TH_PF_420:
            subsamp = TJSAMP_420; break;
        default:
            throw std::runtime_error("Bad subsampling");
    }
        
    if(DEBUG){
        std::cerr << "Decoding: \n";
        std::cerr << "frame: " << tinfo.frame_width << " " << tinfo.frame_height << "\n";
        std::cerr << "pic: " << tinfo.pic_width << " " << tinfo.pic_height << "\n";
    }

    if( tinfo.pic_x != 0 || tinfo.pic_y != 0 )
        throw std::runtime_error("This decoder doesn't support pic_x or pic_y != 0");
        
    auto bsize = tjBufSize(tinfo.pic_width,tinfo.pic_height,subsamp);
    if(DEBUG)
        std::cerr << "Buffer size: " << bsize << " " << tinfo.pic_width << " " << tinfo.pic_height << " " << subsamp << "\n";
        
    std::vector<unsigned char> jbuff( bsize );
    std::vector<char> outF(outfileTemplate.length()+32);
    
    unsigned framesWritten=0;
    while(framesWritten < numFrames){
        bool last = getNextFrame(ctx,buff);

        const unsigned char* planes[3] = {buff[0].data, buff[1].data, buff[2].data };
        int strides[3] = { buff[0].stride, buff[1].stride, buff[2].stride };
        unsigned long jsize = (unsigned long) jbuff.size();
        unsigned char* p = jbuff.data();
        
        tjCompressFromYUVPlanes(
            jhandle, 
            planes, 
            tinfo.pic_width, strides,
            tinfo.pic_height, subsamp,
            &p, &jsize, 85, 0 );
            
            
        if( snprintf( outF.data(), outF.size(), outfileTemplate.c_str(), framesWritten ) >= outF.size() )
            throw std::runtime_error("Filename overflow");
            
        std::ofstream out(outF.data(), std::ios::binary);
        if(!out.good() )
            throw std::runtime_error("Cannot open output file");
            
        out.write( (char*) jbuff.data(), jsize );
            
        std::cout << framesWritten << " ";
        std::cout.flush();
        
        framesWritten++;

        if(last)
            break;
        
    }
    
    std::cout << "\n";
    
    if(ctx)
        th_decode_free(ctx);
    ctx=nullptr;
    ogg_stream_clear(os);
    delete os;
    ogg_sync_clear(&oy);
    tjDestroy(jhandle);
}

    
bool getPacket(){
    int rv=0;
    while(true){
        if(os == nullptr )
            rv = 0;
        else
            rv = ogg_stream_packetout(os,&op);
        
        if( rv == 0 ){
            if(DEBUG){
                std::cerr << "getPacket: Needs to get page\n";
            }
            if( false == getPage() ){
                if( DEBUG ){
                    std::cerr << "in getPacket: getPage failed\n";
                }
                return false;   //EOF
            }
            //else, loop around again
        } else {
            if(DEBUG){
                std::cerr << "getPacket: Got packet successfully\n";
            }
            return true; 
        }
    }
}
        
bool getPage(){
    while(1){
        //take data from oy's buffer and add to og
        int rv = ogg_sync_pageout(&oy,&og);

        if( rv <= 0 ){
            if(DEBUG)
                std::cerr << "getPage: pageout needs more data\n";
            if( false == readFile() ){
                if( DEBUG ){
                    std::cerr << "getPage: readFile returned false\n";
                }
                return false;       //EOF
            }
        } else if( rv  == 1 ){

            int sno = ogg_page_serialno(&og);
             
            //if this page begins a new stream,
            //we must record its serial number and
            //initialize a stream state for it.
            if( ogg_page_bos(&og) ){
                if(DEBUG){
                    std::cerr << "getPage: Saw start of a stream: Serial number=" << sno << "\n";
                }
                if( os == nullptr ){
                    os = new ogg_stream_state();
                    ogg_stream_init(os,sno);
                    theoraStream = sno;
                } else {
                    std::cerr << "Note: Ignoring stream " << sno << "\n";
                    //discard this page
                    continue;
                }
            }

            if( sno != theoraStream )
                continue;       //discard this page
                
            //sets og to have valid data
            //0=ok, -1=failed
            rv =  ogg_stream_pagein(os,&og);
            assert(rv  != -1 );
            return true;
        }
    }
}
    
bool readFile(){
    const int buffsize = 4096;
    //refill oy's buffer with new data from the file
    char* p = ogg_sync_buffer(&oy,buffsize);
    in.read( p, buffsize );
    auto numRead = in.gcount();
    if( DEBUG ){
        std::cerr << "readFile: Got " << numRead << "\n";
    }
    if( numRead == 0 ){
        return false;
    }
    int rv = ogg_sync_wrote(&oy, numRead );
    if( rv < 0 )
        throw std::runtime_error("ogg_sync_wrote error");
    return true;
}
    
// get next frame and store it in the 
//image data. Return true if it's the last frame
bool getNextFrame(th_dec_ctx* ctx, th_ycbcr_buffer& buff){
    
    ogg_int64_t granulepos;
    int rv = th_decode_packetin(ctx,&op,&granulepos);
    if( rv < 0 ){
        //packet decode error. Could keep going and try
        //to re-sync...In that case, return false instead.
        throw std::runtime_error("Packet decode error");
    }
    else if( rv == 1 ){
        //no change from last frame; no need to decode
    } else {
        th_decode_ycbcr_out(ctx,buff);
    }
    
    if( false == getPacket() )
        return true;
    else
        return false;
}
    
    
