/****************************************************************************
Copyright (c) 2010 cocos2d-x.org

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#ifndef __CC_PLATFORM_IMAGE_CPP__
#error "CCFileUtilsCommon_cpp.h can only be included for CCFileUtils.cpp in platform/win32(android,...)"
#endif /* __CC_PLATFORM_IMAGE_CPP__ */

#include "CCImage.h"
#include "CCCommon.h"
#include "CCStdC.h"
#include "CCFileUtils.h"
#include "png.h"
#include "jpeglib.h"
#include <string>
#include <ctype.h>

#if (CC_TARGET_PLATFORM != CC_PLATFORM_IOS) 
// on ios, we should use platform/ios/CCImage_ios.mm instead

typedef struct 
{
    unsigned char* data;
    int size;
    int offset;
}tImageSource;

static void pngReadCallback(png_structp png_ptr, png_bytep data, png_size_t length)
{
    tImageSource* isource = (tImageSource*)png_get_io_ptr(png_ptr);

    if((int)(isource->offset + length) <= isource->size)
    {
        memcpy(data, isource->data+isource->offset, length);
        isource->offset += length;
    }
    else
    {
        png_error(png_ptr, "pngReaderCallback failed");
    }
}

NS_CC_BEGIN

//////////////////////////////////////////////////////////////////////////
// Impliment CCImage
//////////////////////////////////////////////////////////////////////////

CCImage::CCImage()
: m_nWidth(0)
, m_nHeight(0)
, m_nBitsPerComponent(0)
, m_pData(0)
, m_bHasAlpha(false)
, m_bPreMulti(false)
{

}

CCImage::~CCImage()
{
    CC_SAFE_DELETE_ARRAY(m_pData);
}

bool CCImage::initWithImageFile(const char * strPath, EImageFormat eImgFmt/* = eFmtPng*/)
{
    CC_UNUSED_PARAM(eImgFmt);
    CCFileData data(CCFileUtils::fullPathFromRelativePath(strPath), "rb");
    return initWithImageData(data.getBuffer(), data.getSize(), eImgFmt);
}

bool CCImage::initWithImageFileThreadSafe(const char *fullpath, EImageFormat imageType)
{
    CC_UNUSED_PARAM(imageType);
    CCFileData data(fullpath, "rb");
    return initWithImageData(data.getBuffer(), data.getSize(), imageType);
}

bool CCImage::initWithImageData(void * pData, 
                                int nDataLen, 
                                EImageFormat eFmt/* = eSrcFmtPng*/, 
                                int nWidth/* = 0*/,
                                int nHeight/* = 0*/,
                                int nBitsPerComponent/* = 8*/)
{
    bool bRet = false;
    do 
    {
        CC_BREAK_IF(! pData || nDataLen <= 0);

        if (kFmtPng == eFmt)
        {
            bRet = _initWithPngData(pData, nDataLen);
            break;
        }
        else if (kFmtJpg == eFmt)
        {
            bRet = _initWithJpgData(pData, nDataLen);
            break;
        }
        else if (kFmtRawData == eFmt)
        {
            bRet = _initWithRawData(pData, nDataLen, nWidth, nHeight, nBitsPerComponent);
            break;
        }
    } while (0);
    return bRet;
}

bool CCImage::_initWithJpgData(void * data, int nSize)
{
    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    /* libjpeg data structure for storing one row, that is, scanline of an image */
    JSAMPROW row_pointer[1] = {0};
    unsigned long location = 0;
    unsigned int i = 0;

    bool bRet = false;
    do 
    {
        /* here we set up the standard libjpeg error handler */
        cinfo.err = jpeg_std_error( &jerr );

        /* setup decompression process and source, then read JPEG header */
        jpeg_create_decompress( &cinfo );

        jpeg_mem_src( &cinfo, (unsigned char *) data, nSize );

        /* reading the image header which contains image information */
        jpeg_read_header( &cinfo, true );

        // we only support RGB or grayscale
        if (cinfo.jpeg_color_space != JCS_RGB)
        {
            if (cinfo.jpeg_color_space == JCS_GRAYSCALE || cinfo.jpeg_color_space == JCS_YCbCr)
            {
                cinfo.out_color_space = JCS_RGB;
            }
        }
        else
        {
            break;
        }

        /* Start decompression jpeg here */
        jpeg_start_decompress( &cinfo );

        /* init image info */
        m_nWidth  = (short)(cinfo.image_width);
        m_nHeight = (short)(cinfo.image_height);
        m_bHasAlpha = false;
        m_bPreMulti = false;
        m_nBitsPerComponent = 8;
        row_pointer[0] = new unsigned char[cinfo.output_width*cinfo.output_components];
        CC_BREAK_IF(! row_pointer[0]);

        m_pData = new unsigned char[cinfo.output_width*cinfo.output_height*cinfo.output_components];
        CC_BREAK_IF(! m_pData);

        /* now actually read the jpeg into the raw buffer */
        /* read one scan line at a time */
        while( cinfo.output_scanline < cinfo.image_height )
        {
            jpeg_read_scanlines( &cinfo, row_pointer, 1 );
            for( i=0; i<cinfo.image_width*cinfo.num_components;i++) 
                m_pData[location++] = row_pointer[0][i];
        }

        jpeg_finish_decompress( &cinfo );
        jpeg_destroy_decompress( &cinfo );
        /* wrap up decompression, destroy objects, free pointers and close open files */        
        bRet = true;
    } while (0);

    CC_SAFE_DELETE_ARRAY(row_pointer[0]);
    return bRet;
}

bool CCImage::_initWithPngData(void * pData, int nDatalen)
{
// length of bytes to check if it is a valid png file
#define PNGSIGSIZE  8
    bool bRet = false;
    png_byte        header[PNGSIGSIZE]   = {0}; 
    png_structp     png_ptr     =   0;
    png_infop       info_ptr    = 0;

    do 
    {
        // png header len is 8 bytes
        CC_BREAK_IF(nDatalen < PNGSIGSIZE);

        // check the data is png or not
        memcpy(header, pData, PNGSIGSIZE);
        CC_BREAK_IF(png_sig_cmp(header, 0, PNGSIGSIZE));

        // init png_struct
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
        CC_BREAK_IF(! png_ptr);

        // init png_info
        info_ptr = png_create_info_struct(png_ptr);
        CC_BREAK_IF(!info_ptr);

#if (CC_TARGET_PLATFORM != CC_PLATFORM_BADA)
        CC_BREAK_IF(setjmp(png_jmpbuf(png_ptr)));
#endif

        // set the read call back function
        tImageSource imageSource;
        imageSource.data    = (unsigned char*)pData;
        imageSource.size    = nDatalen;
        imageSource.offset  = 0;
        png_set_read_fn(png_ptr, &imageSource, pngReadCallback);

        // read png header info
        
        // read png file info
        png_read_info(png_ptr, info_ptr);
        
        m_nWidth = png_get_image_width(png_ptr, info_ptr);
        m_nHeight = png_get_image_height(png_ptr, info_ptr);
        m_nBitsPerComponent = png_get_bit_depth(png_ptr, info_ptr);
        png_uint_32 channels = png_get_channels(png_ptr, info_ptr);
        png_uint_32 color_type = png_get_color_type(png_ptr, info_ptr);

        CCLOG("color type %u", color_type);
        // only support color type: PNG_COLOR_TYPE_RGB, PNG_COLOR_TYPE_RGB_ALPHA PNG_COLOR_TYPE_PALETTE
        // and expand bit depth to 8
        switch (color_type) {
            case PNG_COLOR_TYPE_RGB:
            case PNG_COLOR_TYPE_RGB_ALPHA:
                // do nothing
                
                break;
            case PNG_COLOR_TYPE_PALETTE:
                png_set_palette_to_rgb(png_ptr);
                channels = 3;
                
                break;
            case PNG_COLOR_TYPE_GRAY:
            case PNG_COLOR_TYPE_GRAY_ALPHA:
                if (m_nBitsPerComponent < 8)
                {
                    png_set_expand_gray_1_2_4_to_8(png_ptr);
                }
                png_set_gray_to_rgb(png_ptr);
                channels = 3;
                
                break;
                
            default:
                CCLog("unsopprted color type %u", color_type);
                goto out;
        }
        if (m_nBitsPerComponent == 16)
        {
            png_set_strip_16(png_ptr);
            m_nBitsPerComponent = 8;
        } 
        
        m_bHasAlpha = (color_type & PNG_COLOR_MASK_ALPHA) ? true : false;
        if (m_bHasAlpha)
        {
            channels = 4;
        }

        // read png data
        // m_nBitsPerComponent will always be 8
        m_pData = new unsigned char[m_nWidth * m_nHeight * channels];
        png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep)*m_nHeight);
        if (row_pointers)
        {
            const unsigned int stride = m_nWidth * channels;
            for (size_t i = 0; i < m_nHeight; ++i)
            {
                png_uint_32 q = i * stride;
                row_pointers[i] = (png_bytep)m_pData + q;
            }
            png_read_image(png_ptr, row_pointers);
            
            if (m_bHasAlpha)
            {
                // premultiply alpha, or the effect will wrong when want to use other pixel format in CCTexture2D,
                // such as RGB888, RGB5A1
#define CC_RGB_PREMULTIPLY_APLHA(vr, vg, vb, va) \
(unsigned)(((unsigned)((unsigned char)(vr) * ((unsigned char)(va) + 1)) >> 8) | \
((unsigned)((unsigned char)(vg) * ((unsigned char)(va) + 1) >> 8) << 8) | \
((unsigned)((unsigned char)(vb) * ((unsigned char)(va) + 1) >> 8) << 16) | \
((unsigned)(unsigned char)(va) << 24))
                
                unsigned int *tmp = (unsigned int *)m_pData;
                for(unsigned int i = 0; i < m_nHeight; i++)
                {
                    for(int j = 0; j < m_nWidth * channels; j += 4)
                    {
                        *tmp++ = CC_RGB_PREMULTIPLY_APLHA( row_pointers[i][j], row_pointers[i][j + 1], 
                                                          row_pointers[i][j + 2], row_pointers[i][j + 3] );
                    }
                }
                
                m_bPreMulti = true;
            }

            free(row_pointers);
            bRet = true;
        }
    } while (0);

out:
    if (png_ptr)
    {
        png_destroy_read_struct(&png_ptr, (info_ptr) ? &info_ptr : 0, 0);
    }
    return bRet;
}

bool CCImage::_initWithRawData(void * pData, int nDatalen, int nWidth, int nHeight, int nBitsPerComponent)
{
    bool bRet = false;
    do 
    {
        CC_BREAK_IF(0 == nWidth || 0 == nHeight);

        m_nBitsPerComponent = nBitsPerComponent;
        m_nHeight   = (short)nHeight;
        m_nWidth    = (short)nWidth;
        m_bHasAlpha = true;

        // only RGBA8888 surported
        int nBytesPerComponent = 4;
        int nSize = nHeight * nWidth * nBytesPerComponent;
        m_pData = new unsigned char[nSize];
        CC_BREAK_IF(! m_pData);
        memcpy(m_pData, pData, nSize);

        bRet = true;
    } while (0);
    return bRet;
}

bool CCImage::saveToFile(const char *pszFilePath, bool bIsToRGB)
{
    bool bRet = false;

    do 
    {
        CC_BREAK_IF(NULL == pszFilePath);

        std::string strFilePath(pszFilePath);
        CC_BREAK_IF(strFilePath.size() <= 4);

        std::string strLowerCasePath(strFilePath);
        for (unsigned int i = 0; i < strLowerCasePath.length(); ++i)
        {
            strLowerCasePath[i] = tolower(strFilePath[i]);
        }

        if (std::string::npos != strLowerCasePath.find(".png"))
        {
            CC_BREAK_IF(!_saveImageToPNG(pszFilePath, bIsToRGB));
        }
        else if (std::string::npos != strLowerCasePath.find(".jpg"))
        {
            CC_BREAK_IF(!_saveImageToJPG(pszFilePath));
        }
        else
        {
            break;
        }

        bRet = true;
    } while (0);

    return bRet;
}

bool CCImage::_saveImageToPNG(const char * pszFilePath, bool bIsToRGB)
{
    bool bRet = false;
    do 
    {
        CC_BREAK_IF(NULL == pszFilePath);

        FILE *fp;
        png_structp png_ptr;
        png_infop info_ptr;
        png_colorp palette;
        png_bytep *row_pointers;

        fp = fopen(pszFilePath, "wb");
        CC_BREAK_IF(NULL == fp);

        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

        if (NULL == png_ptr)
        {
            fclose(fp);
            break;
        }

        info_ptr = png_create_info_struct(png_ptr);
        if (NULL == info_ptr)
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, NULL);
            break;
        }
#if (CC_TARGET_PLATFORM != CC_PLATFORM_BADA)
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }
#endif
        png_init_io(png_ptr, fp);

        if (!bIsToRGB && m_bHasAlpha)
        {
            png_set_IHDR(png_ptr, info_ptr, m_nWidth, m_nHeight, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        } 
        else
        {
            png_set_IHDR(png_ptr, info_ptr, m_nWidth, m_nHeight, 8, PNG_COLOR_TYPE_RGB,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        }

        palette = (png_colorp)png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH * sizeof (png_color));
        png_set_PLTE(png_ptr, info_ptr, palette, PNG_MAX_PALETTE_LENGTH);

        png_write_info(png_ptr, info_ptr);

        png_set_packing(png_ptr);

        row_pointers = (png_bytep *)malloc(m_nHeight * sizeof(png_bytep));
        if(row_pointers == NULL)
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }

        if (!m_bHasAlpha)
        {
            for (int i = 0; i < (int)m_nHeight; i++)
            {
                row_pointers[i] = (png_bytep)m_pData + i * m_nWidth * 3;
            }

            png_write_image(png_ptr, row_pointers);

            free(row_pointers);
            row_pointers = NULL;
        }
        else
        {
            if (bIsToRGB)
            {
                unsigned char *pTempData = new unsigned char[m_nWidth * m_nHeight * 3];
                if (NULL == pTempData)
                {
                    fclose(fp);
                    png_destroy_write_struct(&png_ptr, &info_ptr);
                    break;
                }

                for (int i = 0; i < m_nHeight; ++i)
                {
                    for (int j = 0; j < m_nWidth; ++j)
                    {
                        pTempData[(i * m_nWidth + j) * 3] = m_pData[(i * m_nWidth + j) * 4];
                        pTempData[(i * m_nWidth + j) * 3 + 1] = m_pData[(i * m_nWidth + j) * 4 + 1];
                        pTempData[(i * m_nWidth + j) * 3 + 2] = m_pData[(i * m_nWidth + j) * 4 + 2];
                    }
                }

                for (int i = 0; i < (int)m_nHeight; i++)
                {
                    row_pointers[i] = (png_bytep)pTempData + i * m_nWidth * 3;
                }

                png_write_image(png_ptr, row_pointers);

                free(row_pointers);
                row_pointers = NULL;

                CC_SAFE_DELETE_ARRAY(pTempData);
            } 
            else
            {
                for (int i = 0; i < (int)m_nHeight; i++)
                {
                    row_pointers[i] = (png_bytep)m_pData + i * m_nWidth * 4;
                }

                png_write_image(png_ptr, row_pointers);

                free(row_pointers);
                row_pointers = NULL;
            }
        }

        png_write_end(png_ptr, info_ptr);

        png_free(png_ptr, palette);
        palette = NULL;

        png_destroy_write_struct(&png_ptr, &info_ptr);

        fclose(fp);

        bRet = true;
    } while (0);
    return bRet;
}
bool CCImage::_saveImageToJPG(const char * pszFilePath)
{
    bool bRet = false;
    do 
    {
        CC_BREAK_IF(NULL == pszFilePath);

        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        FILE * outfile;                 /* target file */
        JSAMPROW row_pointer[1];        /* pointer to JSAMPLE row[s] */
        int     row_stride;          /* physical row width in image buffer */

        cinfo.err = jpeg_std_error(&jerr);
        /* Now we can initialize the JPEG compression object. */
        jpeg_create_compress(&cinfo);

        CC_BREAK_IF((outfile = fopen(pszFilePath, "wb")) == NULL);
        
        jpeg_stdio_dest(&cinfo, outfile);

        cinfo.image_width = m_nWidth;    /* image width and height, in pixels */
        cinfo.image_height = m_nHeight;
        cinfo.input_components = 3;       /* # of color components per pixel */
        cinfo.in_color_space = JCS_RGB;       /* colorspace of input image */

        jpeg_set_defaults(&cinfo);

        jpeg_start_compress(&cinfo, TRUE);

        row_stride = m_nWidth * 3; /* JSAMPLEs per row in image_buffer */

        if (m_bHasAlpha)
        {
            unsigned char *pTempData = new unsigned char[m_nWidth * m_nHeight * 3];
            if (NULL == pTempData)
            {
                jpeg_finish_compress(&cinfo);
                jpeg_destroy_compress(&cinfo);
                fclose(outfile);
                break;
            }

            for (int i = 0; i < m_nHeight; ++i)
            {
                for (int j = 0; j < m_nWidth; ++j)

                {
                    pTempData[(i * m_nWidth + j) * 3] = m_pData[(i * m_nWidth + j) * 4];
                    pTempData[(i * m_nWidth + j) * 3 + 1] = m_pData[(i * m_nWidth + j) * 4 + 1];
                    pTempData[(i * m_nWidth + j) * 3 + 2] = m_pData[(i * m_nWidth + j) * 4 + 2];
                }
            }

            while (cinfo.next_scanline < cinfo.image_height) {
                row_pointer[0] = & pTempData[cinfo.next_scanline * row_stride];
                (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }

            CC_SAFE_DELETE_ARRAY(pTempData);
        } 
        else
        {
            while (cinfo.next_scanline < cinfo.image_height) {
                row_pointer[0] = & m_pData[cinfo.next_scanline * row_stride];
                (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }
        }

        jpeg_finish_compress(&cinfo);
        fclose(outfile);
        jpeg_destroy_compress(&cinfo);
        
        bRet = true;
    } while (0);
    return bRet;
}

NS_CC_END

#endif // (CC_TARGET_PLATFORM != TARGET_OS_IPHONE)
/* ios/CCImage_ios.mm uses "mm" as the extension, 
   so we cannot inclue it in this CCImage.cpp.
   It makes a little difference on ios */
