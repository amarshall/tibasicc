/*
 * Copyright (c) 2011 Matthew Iselin
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include <stdio.h>
#include <string.h>

#include "tibasic.h"

using namespace std;

/* \todo More error handling. */

unsigned short Compiler::doChecksum(size_t sum)
{
    /* Bottom 16 bits of the sum. */
    return (unsigned short) (sum & 0xFFFF);
}

size_t Compiler::sumBytes(const char *data, size_t len)
{
    size_t ret = 0;
    for(size_t i = 0; i < len; i++)
        ret += data[i];
    return ret;
}

string trim(const string& str)
{
    size_t first = str.find_first_not_of(' ');
    if (string::npos == first)
    {
        return str;
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

extern bool verbose;

bool Compiler::compile(string inFile, string outFile)
{
    ifstream f(inFile.c_str(), ifstream::in);

    string tmpLine;

    /* Output information ready for writing the compiled code to a file. */
    vector<token_t> output;
    unsigned short outputSize = 0;

    while(!f.eof())
    {
        getline(f, tmpLine, '\n');

        /* ignore empty lines */
        if(!tmpLine.length())
        {
            if(verbose)
                log(Debug, "Empty line detected!");
            continue;
        }

        /* remove comments */
        tmpLine = tmpLine.substr(0, tmpLine.find("#", 0));

        /* strip spaces at the beginning and end of lines */
        tmpLine = trim(tmpLine);


        /* ignore lines with now only whitespaces */
        bool containsSpaces = tmpLine.find_first_not_of(' ') != std::string::npos;
        if(!containsSpaces)
        {
            if(verbose)
                log(Debug, "Line with only whitespaces / comments detected!");
            continue;
        }


        /* Parse. */
        token_t token;

        while(tmpLine.length())
        {
            /* Grab the longest possible token we can from the input. */
            string s = tmpLine.substr(0, getLongestToken());

            bool validToken = false;
            while(!validToken && s.length())
            {
                validToken = lookupToken(s, token);
                if(!validToken)
                    s = s.substr(0, s.length() - 1);
            }

            /* Special case for alphabet characters */
            if(!s.length() && isalpha(tmpLine[0]))
            {
                token.token = toupper(tmpLine[0]);
                token.sz = 1;

                s = tmpLine.substr(0, 1);
            }

            if(!s.length())
            {
                /* Error, asplode! */
                log(Error, "Invalid token.");
                f.close();
                return false;
            }
            else
            {
                outputSize += token.sz;
                output.push_back(token);
                if(verbose)
                    std::cout << "Debug: '" << s << "'\n";
                tmpLine = tmpLine.substr(s.length(), tmpLine.length());
            }
        }

        /* Output a newline. */
        bool gotNewline = lookupToken("\n", token);
        if(gotNewline)
        {
            outputSize += token.sz;
            output.push_back(token);
        }
    }

    /* Have the file read and parsed now. Time to write the output. */
    struct ProgramHeader phdr; struct VariableEntry ventry;
    memset(&phdr, 0, sizeof(ProgramHeader));
    memset(&ventry, 0, sizeof(VariableEntry));
    
    phdr.datalen = sizeof(VariableEntry) + outputSize + sizeof(outputSize);
    strcpy(phdr.sig, "**TI83F*");
    phdr.extsig[0] = 0x1A; phdr.extsig[1] = 0x0A; phdr.extsig[2] = 0;
    strcpy(phdr.comment, "Generated by the TI-BASIC Compiler.");

    /* \todo Magic numbers! */
    ventry.start = 0x0D;
    ventry.length1 = ventry.length2 = outputSize + sizeof(outputSize);
    ventry.type = 0x05;

    /* Convoluted magic to get the filename. Minus the extension. :) */
    size_t i = 0;
    size_t n = outFile.find_last_of('/');
    if(n == inFile.npos) n = outFile.find_last_of('\\');
    if(n == inFile.npos) n = 0; else n++;
    for(; (i < 8) && (n < inFile.length() - 4); n++)
    {
        if(outFile[n] == '.')
            break;
        ventry.name[i++] = toupper(outFile[n]);
    }

    /* Begin writing to file. */
    FILE *out = fopen(outFile.c_str(), "wb");
    fwrite(&phdr, sizeof(phdr), 1, out);
    fwrite(&ventry, sizeof(ventry), 1, out);
    fwrite(&outputSize, 2, 1, out);

    /* Sum of all bytes for checksum purposes. */
    size_t sum = 0;

    for(vector<token_t>::iterator it = output.begin();
        it != output.end();
        ++it)
    {
        fwrite(&(it->token), it->sz, 1, out);
        sum += it->token;
    }

    /* Perform a checksum and write to file. */
    sum += outputSize;
    sum += sumBytes(reinterpret_cast<const char*>(&ventry), sizeof(ventry));
    unsigned short checksum = doChecksum(sum);
    fwrite(&checksum, 2, 1, out);

    fclose(out);

    return true;
}

bool Compiler::decompile(string inFile, string outFile)
{
    /* Parse the file. */
    FILE *fp = fopen(inFile.c_str(), "rb");
    if(!fp)
    {
        log(Error, "Couldn't open input file.");
        return false;
    }

    /* \todo Checksum verification. */

    /* File header */
    struct ProgramHeader phdr;
    fread(&phdr, sizeof(phdr), 1, fp);

    /* Variable entry */
    struct VariableEntry ventry;
    fread(&ventry, sizeof(ventry), 1, fp);

    size_t tokenLength = 0;
    fread(&tokenLength, 2, 1, fp);

    size_t nBytesRead = 0;
    unsigned short temp;

    string sOutput = "";

    bool bAsmProgram = false;

    while((!feof(fp)) && (nBytesRead < tokenLength))
    {
        fread(&temp, 1, 2, fp);

        /* If we're in assembly mode, just copy the bytes straight in a numbers. */
        if(bAsmProgram)
        {
            if(((temp & 0xFF) == 0x3F))
                sOutput += "\n";
            sOutput += temp & 0xFF;

            fseek(fp, -1, SEEK_CUR);
            nBytesRead++;

            continue;
        }

        /* Convoluted. */
        string conv;
        bool bIsFound = lookupToken(temp, conv);
        if(!bIsFound)
            bIsFound = lookupToken(temp & 0xFF, conv);

        if(!bIsFound)
        {
            sOutput += static_cast<char>(temp);

            fseek(fp, -1, SEEK_CUR);
            nBytesRead++;
        }
        else
        {
            sOutput += conv;

            token_t tokenInfo;
            lookupToken(conv, tokenInfo);

            if(tokenInfo.sz < sizeof(unsigned short))
            {
                fseek(fp, -1, SEEK_CUR);
                nBytesRead++;
            }
            else
                nBytesRead += 2;

            if(conv == "AsmPrgm")
                bAsmProgram = !bAsmProgram;
        }
    }

    fclose(fp);

    /* Write the output now. */
    ofstream f(outFile.c_str());
    f << sOutput;
    f.close();

    /* \todo error handling for output file. */

    return true;
}
