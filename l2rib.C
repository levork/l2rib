/*
 * l2rib.C version 1.00 - http://www.levork.org/l2rib.html
 *
 * Copyright (C) 2001-2005 by Julian Fong (http://www.levork.org/).  All
 * rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * The RenderMan (R) Interface Procedures and RIB Protocol are:
 * Copyright 1988, 1989, Pixar. All rights reserved.
 * RenderMan (R) is a registered trademark of Pixar.
 *
 * This is l2rib, a program for converting LDraw .DAT files to
 * RenderMan Interface Bytestream .RIB files for use in a RenderMan
 * compliant renderer. In practice, this has primarily been tested
 * with one renderer: Pixar's PhotoRealistic RenderMan.
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/time.h>
#include <unistd.h>
#else
#include <time.h>
#include <windows.h>
#include <shfolder.h>
#endif
#include <math.h>

#include <string>
#ifdef _WIN32
#include <iostream>
#include <sstream>
#include <fstream>
#include <direct.h>
#else
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#endif
#include <vector>
#include <map>
#include <set>
#if __GNUC__ >= 3
#include <ext/hash_map>
using namespace __gnu_cxx;
// This is stupid.
// (see http://gcc.gnu.org/bugzilla/show_bug.cgi?id=13342)
namespace __gnu_cxx
{
    using namespace std;
    
    template<>
    struct hash<string>
    {
	size_t operator()(const string& s) const
	{
	    const collate<char>& c = use_facet<collate<char> >(locale::classic());
	    return c.hash(s.c_str(), s.c_str() + s.size());
	}
    };
}
#elif __GNUC__ >= 2
#include <hash_map>
template<> struct hash<std::string>                                          
            
{                                                                            
              
    size_t operator()(const std::string& __x) const 
    { 
	return __stl_hash_string(__x.c_str()); 
    }                                                                          
              
};                    

#else
#include <hash_map>
#endif

#ifdef _WIN32
#define PATHSEP "\\"
#else
#define PATHSEP "/"
#endif

#define REMOVE_SPACES(x) x.erase(std::remove(x.begin(), x.end(), ' '), x.end())
#define REMOVE_CRS(x) x.erase(std::remove(x.begin(), x.end(), '\r'), x.end())

using namespace std;

struct ColourCode {
    ColourCode() : r(0), g(0), b(0), shader(0), transparent(false), transparency(0), init(false), warned(false), custom(false), edger(0), edgeg(0), edgeb(0), edgecode(-1) {}
    string name;
    float r, g, b;
    int shader;
    bool transparent;
    float transparency;
    bool init;
    bool warned;
    bool custom;
    float edger, edgeg, edgeb;
    int edgecode;
    string customShader;
};

struct Point {
    Point(float _x = 0, float _y = 0, float _z = 0) : x(_x), y(_y), z(_z) {}
    Point(const Point& p) : x(p.x), y(p.y), z(p.z) {}
    float x;
    float y;
    float z;
    Point& operator= (const Point& p) { x = p.x; y = p.y; z = p.z; return *this;}
    Point& operator+= (const Point& p) { x += p.x; y += p.y; z += p.z; return *this;}
};

ostream& operator<< (ostream& o, const Point& p) {
    return o << p.x << ' ' << p.y << ' ' << p.z;
}

istream& operator>> (istream& i, Point& p) {
    i >> p.x >> p.y >> p.z;
    return i;
}

Point operator+(const Point& p1, const Point& p2) {
    return Point(p1.x + p2.x, p1.y + p2.y, p1.z + p2.z);
}

Point operator- (const Point& p1, const Point& p2) {
    return Point(p1.x - p2.x, p1.y - p2.y, p1.z - p2.z);
}

bool operator< (const Point& p1, const Point& p2) {
    if (p1.x > p2.x) return false;
    if (p1.x < p2.x) return true;

    if (p1.y > p2.y) return false;
    if (p1.y < p2.y) return true;

    if (p1.z > p2.z) return false;
    if (p1.z < p2.z) return true;

    // all compare the same
    return false;
}

struct Bound {
    Bound() : init(false), mpdincomplete(false) {}
    float minx;
    float maxx;
    float miny;
    float maxy;
    float minz;
    float maxz;
    bool init;
    // This flag indicates whether the bound is incomplete due to a
    // forward part reference to a MPD file. In that case, the bound
    // cannot be accurate until another pass through the file.
    bool mpdincomplete;
    Bound& operator=(const Point &p) {
	minx = maxx = p.x;
	miny = maxy = p.y;
	minz = maxz = p.z;
	init = true;
	return *this;
    }
    void expand(const Point &p) {
	if (!init) *this = p;
	else {
	    if (p.x < minx) minx = p.x;
	    if (p.x > maxx) maxx = p.x;
	    if (p.y < miny) miny = p.y;
	    if (p.y > maxy) maxy = p.y;
	    if (p.z < minz) minz = p.z;
	    if (p.z > maxz) maxz = p.z;
	}
    }
    void expand(const Bound& newbound, float* matrix) {
	if (!newbound.init) return;
	// Transform all 8 corners of the bounding box
	Point p[8] = {
	    Point (
		   matrix[0] * newbound.minx + matrix[4] * newbound.miny + matrix[8] * newbound.minz + matrix[12],
		   matrix[1] * newbound.minx + matrix[5] * newbound.miny + matrix[9] * newbound.minz + matrix[13],
		   matrix[2] * newbound.minx + matrix[6] * newbound.miny + matrix[10] * newbound.minz + matrix[14]
		   ),
	    Point ( 
		   matrix[0] * newbound.minx + matrix[4] * newbound.miny + matrix[8] * newbound.maxz + matrix[12],
		   matrix[1] * newbound.minx + matrix[5] * newbound.miny + matrix[9] * newbound.maxz + matrix[13],
		   matrix[2] * newbound.minx + matrix[6] * newbound.miny + matrix[10] * newbound.maxz + matrix[14]
		   ),
	    Point (
		   matrix[0] * newbound.minx + matrix[4] * newbound.maxy + matrix[8] * newbound.minz + matrix[12],
		   matrix[1] * newbound.minx + matrix[5] * newbound.maxy + matrix[9] * newbound.minz + matrix[13],
		   matrix[2] * newbound.minx + matrix[6] * newbound.maxy + matrix[10] * newbound.minz + matrix[14]
		   ),
	    Point (
		   matrix[0] * newbound.minx + matrix[4] * newbound.maxy + matrix[8] * newbound.maxz + matrix[12],
		   matrix[1] * newbound.minx + matrix[5] * newbound.maxy + matrix[9] * newbound.maxz + matrix[13],
		   matrix[2] * newbound.minx + matrix[6] * newbound.maxy + matrix[10] * newbound.maxz + matrix[14]
		   ),
	    Point (
		   matrix[0] * newbound.maxx + matrix[4] * newbound.miny + matrix[8] * newbound.minz + matrix[12],
		   matrix[1] * newbound.maxx + matrix[5] * newbound.miny + matrix[9] * newbound.minz + matrix[13],
		   matrix[2] * newbound.maxx + matrix[6] * newbound.miny + matrix[10] * newbound.minz + matrix[14]
		   ),
	    Point (
		   matrix[0] * newbound.maxx + matrix[4] * newbound.miny + matrix[8] * newbound.maxz + matrix[12],
		   matrix[1] * newbound.maxx + matrix[5] * newbound.miny + matrix[9] * newbound.maxz + matrix[13],
		   matrix[2] * newbound.maxx + matrix[6] * newbound.miny + matrix[10] * newbound.maxz + matrix[14]
		   ),
	    Point (
		   matrix[0] * newbound.maxx + matrix[4] * newbound.maxy + matrix[8] * newbound.minz + matrix[12],
		   matrix[1] * newbound.maxx + matrix[5] * newbound.maxy + matrix[9] * newbound.minz + matrix[13],
		   matrix[2] * newbound.maxx + matrix[6] * newbound.maxy + matrix[10] * newbound.minz + matrix[14]
		   ),
	    Point (
		   matrix[0] * newbound.maxx + matrix[4] * newbound.maxy + matrix[8] * newbound.maxz + matrix[12],
		   matrix[1] * newbound.maxx + matrix[5] * newbound.maxy + matrix[9] * newbound.maxz + matrix[13],
		   matrix[2] * newbound.maxx + matrix[6] * newbound.maxy + matrix[10] * newbound.maxz + matrix[14]
		   )
	};
	for (int i = 0; i < 8; ++i) {
	    expand(p[i]);
	}
    }
};

ostream& operator<< (ostream& o, const Bound& bound) {
    return o << bound.minx << ' ' << bound.maxx << ' ' << bound.miny << ' ' << bound.maxy << ' ' << bound.minz << ' ' << bound.maxz;
}

istream& operator>> (istream& i, Bound& bound) {
    i >> bound.minx >> bound.maxx >> bound.miny >> bound.maxy >> bound.minz >> bound.maxz;
    return i;
}

//////////////////////////////////////////////////
// Globals
//////////////////////////////////////////////////

ColourCode ColourCodes[512];

bool doRaytrace = false;
bool doLines = false;
bool doStudLogo = false;
bool doDRA = true;
string ldrawdir;
string l2ribdir;
string cachedir;
string colorcfg;

float bgcolor[3] = {1.0, 1.0, 1.0};
float camDistance = 1.5;
Point cameraFrom(-1, -1, -1);
Point cameraTo(0, 0, 0);
Point cameraUp(0, -1, 0);
float floorScale = 0;
vector<Point> lightPositions;
vector<float> lightColours;
vector<float> lightIntensities;
vector<string> lightShadowTypes;
int pixelSamples = 2;
float shadingRate = 1.0;
time_t programTime;
bool usecache = true;
int formatX = 640;
int formatY = 480;
int shadowFormat = 1024;

// MPD processing
bool doMPD = false;
set<string> mpdNames;
hash_map<string, Bound> mpdBounds;

const string searchpath[] = {
    "p" PATHSEP "48",
    "p",
    "parts",
    "models"
};


//////////////////////////////////////////////////

bool parseFile(ostream& out, ifstream& in, const string& partname, Bound& bound);

bool fileExists(string filename, bool checkTime=false) {
    struct stat buf;
    if (stat(filename.c_str(), &buf) == 0) {
	if (checkTime) {
	    // check the time stamp on the file. If it's older than
	    // the app runtime, we assume the file does not exist
	    // and needs to be clobbered
	    return (buf.st_mtime >= programTime);
	}
	return true;
    } else {
	return false;
    }
}

//////////////////////////////////////////////////
// String handling
//////////////////////////////////////////////////

string tokenize(string& s)
{
    string::size_type length = s.length();
    if (length == 0)
	return s;
    string::size_type start;
    // Ignore leading space
    for (start = 0; start < length; ++start) {
	if (!isspace(s[start])) {
	    break;
	}
    }
    for (string::size_type i = start + 1; i < length; ++i) {
	if (isspace(s[i])) {
	    string retval = s.substr(start, i - start);
	    s = s.substr(i);
	    return retval;
	}
    }
    string retval = s.substr(start, length - start);
    s = "";
    return retval;
}

bool isWhitespaceString(const string& s) {
    string::size_type length = s.length();
    if (length == 0)
	return true;
    string::size_type start;
    for (start = 0; start < length; ++start) {
	if (!isspace(s[start])) {
	    return false;
	}
    }
    return true;
}

bool isNumericString(const string& s) {
    for (string::const_iterator i = s.begin(); i != s.end(); i++)
	if (!isdigit(*i))
	    return false;
    return true;
}

bool isFloatString(const string& s) {
    float t;
    return (sscanf(s.c_str(), "%f", &t) == 1);
}

string fixFileName(const string& s)
{
    string retval(s);
    REMOVE_SPACES(retval);
    REMOVE_CRS(retval);
    
    for (string::iterator i = retval.begin(); i != retval.end(); i++) {
	if (*i == '\\')
	    *i = '/';
        else if (*i == ' ')
            *i = '_';
        else
	    *i = tolower(*i);
    }
    return retval;
}

// Fix file names when they get written to RIB
string fixRIBFileName(const string& s)
{
    string retval;
    for (string::const_iterator i = s.begin(); i != s.end(); i++) {
	if (*i == '\\')
	    retval += "/";
	else
	    retval += tolower(*i);
    }
    return retval;
}

//////////////////////////////////////////////////
// Command handling
//////////////////////////////////////////////////

// Stores bounds in memory to avoid going to disk
map<string, Bound> boundsMap;

void getBound(Bound& bound, const string& filename, const string& partname) {
    map<string, Bound>::const_iterator i = boundsMap.find(partname);
    if (i != boundsMap.end()) {
	bound = i->second;
    } else {
	// We have to read the first line from the file
	ifstream in(filename.c_str());
	if (!in) {
	    cerr << "Unable to read " << filename << " for bounds computation\n";
	    return;
	}
	string line;
	getline(in, line);
	if (tokenize(line) == "Bound") {
	    istringstream lineStream(line.c_str());
	    lineStream >> bound;
	    bound.init = true;
	    boundsMap[partname] = bound;
	} else {
	    cerr << "Unable to read bound from " << filename << "\n";
	}
    }
}

void parseColour(string line) {
    string command, value;
    ColourCode c;
    int code = -1;
    bool edgevalid = false;
    c.name = tokenize(line);
    do {
	command = tokenize(line);
	if (command.empty()) break;
	if (command == "CODE") {
	    value = tokenize(line);
	    if (value.empty()) break;
	    code = atoi(value.c_str());
	}
	else if (command == "VALUE") {
	    value = tokenize(line);
	    if (value.empty()) break;
	    // Parse hex
	    unsigned int r, g, b;
	    if (sscanf(value.c_str(), "#%2x%2x%2x\n", &r, &g, &b) != 3 && sscanf(value.c_str(), "0x%2x%2x%2x\n", &r, &g, &b) != 3) break;
	    c.r = r / 255.0f;
	    c.g = g / 255.0f;
	    c.b = b / 255.0f;
	}
	else if (command == "LUMINANCE") {
	    // currently ignored
	    value = tokenize(line);
	    if (value.empty()) break;
	}
	else if (command == "EDGE") {
	    value = tokenize(line);
	    if (value.empty()) break;
	    edgevalid = true;
	    // Check whether the edge looks like a code
	    if (isNumericString(value)) {	    
		c.edgecode = atoi(value.c_str());
	    } else {
		// Parse hex
		unsigned int r, g, b;
		if (sscanf(value.c_str(), "#%2x%2x%2x\n", &r, &g, &b) != 3 && sscanf(value.c_str(), "0x%2x%2x%2x\n", &r, &g, &b) != 3) break;
		c.edger = r / 255.0f;
		c.edgeg = g / 255.0f;
		c.edgeb = b / 255.0f;
	    }
	}	    
	else if (command == "ALPHA") {
	    value = tokenize(line);
	    if (value.empty()) break;
	    c.transparent = true;
	    c.transparency = atoi(value.c_str()) / 255.0f;
	    c.shader = 3;
	}
	else if (command == "CHROME" || command == "METALLIC" || command == "MATTE_METALLIC") {
	    // okay, these really should be differentiated.
	    c.shader = 2;
	}
	else if (command == "RUBBER") {
	    c.shader = 4;
	}
	else if (command == "PEARLESCENT") {
	    // not yet implemented
	}
	else if (command == "MATERIAL") {
	    // Parse the rest of the line as a "shader" definition
	    c.custom = true;
	    c.customShader = line;
	    break;
	}
    } while (!command.empty());
    if (code != -1 && code < 512) {
	c.init = true;
	// If we never saw an edge, synthesize one
	if (!edgevalid) {
	    c.edger = 1.0f - c.r;
	    c.edgeg = 1.0f - c.g;
	    c.edgeb = 1.0f - c.b;
	}
	ColourCodes[code] = c;
    }
}

void writeColour(ostream& out, string colour) {
    if (isNumericString(colour)) {
	int colIndex = atoi(colour.c_str());
	if (colIndex < 0 || colIndex >= 512) {
	    cerr << "Invalid color: " <<  colIndex << '\n';
	    out << "Color 1.0 0.0 0.0\n";
	    out << "Attribute \"user\" \"uniform color l2ribEdgeColor\" [0.0 1.0 1.0]\n";
	    return;
	}
	if (colIndex == 16) {
	    // Inherit the previous color/opacity, so don't output
	    // anything and the stack will take care of it.
	    return;
	}
	if (colIndex == 24) {
	    // Because we're using ReadArchives we can't just write
	    // out a color - this would bake a reversed color into the
	    // RIB and if the RIB gets read by another file later on
	    // the color would be wrong. The solution is to use a
	    // shader which examines the user attribute "edgecolor".
	    out << "Surface \"edgeConstant\"\n";
	    return;
	}	    
	ColourCode& c = ColourCodes[colIndex];
	if (!c.init) {
	    // Synthesize a dithered color if we can
	    if (colIndex > 256) {
		int colIndexA, colIndexB;
		colIndexA = (colIndex - 256) % 16;
		colIndexB = (colIndex - 256) / 16;
		ColourCode&a = ColourCodes[colIndexA];
		ColourCode&b = ColourCodes[colIndexB];
		if (a.init && b.init) {
		    c.name = "Dither of " + a.name + " and " + b.name;
		    c.r = 0.5 * (a.r + b.r);
		    c.g = 0.5 * (a.g + b.g);
		    c.b = 0.5 * (a.b + b.b);
		    if (a.transparent || b.transparent) {
			c.transparent = true;
		    }
		    c.init = true;
		    cerr << "Warning: unknown color code " << colIndex << ", will use a dither of " << a.name << " and " << b.name << "\n";
		} else {
		    if (!c.warned) {
			cerr << "Warning: unknown color code " << colIndex << " encountered, and unable to use dither\n";
			c.warned = true;
		    }
		    out << "# Unknown color code: " << colIndex << "\n";
		    out << "Color 1.0 0.0 0.0\n";
		    out << "Attribute \"user\" \"uniform color l2ribEdgeColor\" [0.0 1.0 1.0]\n";		    
		    return;
		}
	    } else {
		if (!c.warned) {
		    cerr << "Warning: unknown color code " << colIndex << " encountered\n";
		    c.warned = true;
		}
		out << "# Unknown color code: " << colIndex << "\n";
		out << "Color 1.0 0.0 0.0\n";
		out << "Attribute \"user\" \"uniform color l2ribEdgeColor\" [0.0 1.0 1.0]\n";		
		return;
	    }
	}

	out << "# " << c.name << "\n";
	if (c.custom) {
	    out << "# custom material\n";
	    out << c.customShader << "\n";
	    return;
	}
	if (c.transparent) {
	    out << "Opacity " << c.transparency << ' ' << c.transparency << ' ' << c.transparency << '\n';
	}
	out << "Color " << c.r << ' ' << c.g << ' ' << c.b << '\n';
	out << "Attribute \"user\" \"uniform color l2ribEdgeColor\" [" << c.edger << ' ' << c.edgeg << ' ' << c.edgeb << "]\n";
	switch (c.shader) {
	    case 0:
		break;
	    case 1:
		out << "Surface \"plastic\" \"Ks\" [0.8]\n";
		break;
	    case 2:
		out << "Surface \"metal\"\n";
		break;
	    case 3:
		out << "Surface \"glass\" \"Kd\" [0.3] \"uniform float eta\" [1.33] \"uniform float refrraysamples\" [3] \n";
		break;
	    case 4:
		// Matte seems as good as anything for rubber
		out << "Surface \"matte\"\n";
		break;
	}
    } else {
	// MLCad and L3P extended color syntax. From the description
	// on L3P's home page.
	unsigned hex;
	if (sscanf(colour.c_str(), "%x", &hex) == 1) {
	    // L3P color syntax
	    if (hex & 0x02000000) {
		out << "Color " << ((hex & 0x00FF0000) >> 16) / 255.0f << ' ' <<  ((hex & 0x0000FF00) >> 8) / 255.0f << ' ' << (hex & 0x000000FF) / 255.0f << '\n';
		out << "Opacity 1 1 1\n";
	    } else if (hex & 0x03000000) {
		out << "Color " << ((hex & 0x00FF0000) >> 16) / 255.0f << ' ' << ((hex & 0x0000FF00) >> 8) / 255.0f << ' ' <<  (hex & 0x000000FF) / 255.0f << '\n';
		out << "Opacity 0.5 0.5 0.5\n";
	    }
	    out << "Surface \"plastic\"\n";
	}
	else {
	    cerr << "Unknown colour " << colour << "\n";
	}
    }
}

void writeMatrix(ostream& out, float* matrix) {
    out << "ConcatTransform [ "
	<< matrix[0] << ' ' << matrix[1] << ' ' << matrix[2] << ' ' << matrix[3] << ' '
	<< matrix[4] << ' ' << matrix[5] << ' ' << matrix[6] << ' ' << matrix[7] << ' '
	<< matrix[8] << ' ' << matrix[9] << ' ' << matrix[10] << ' ' << matrix[11] << ' '
	<< matrix[12] << ' ' << matrix[13] << ' ' << matrix[14] << ' ' << matrix[15] << "]\n";
}

void insertPart(ostream& out, string colour, float* matrix, const string& partname, Bound& bound) {
    string realfile;

    bool found = false;
    bool isPart = false;
    bool isMPD = false;

    // Handle the special case of p/48/part.dat by ignoring the 48
    // initially; we'll defer to the search path instead. This gives
    // us a chance to insert RIB substitutions instead
    string realpart;
    if (partname.length() > 3 && partname.substr(0, 2) == "48" && (partname[2] == '\\' || partname[2] == '/')) {
	realpart = partname.substr(3, string::npos);
    } else {
	realpart = partname;
    }

    // Check for MPD file
    if (mpdNames.find(partname) != mpdNames.end()) {
	found = true;
	isMPD = true;
    }

    // Look for file in search path
    if (!found) {
	for (int i = 0; i < 4; ++i) {
	    realfile = ldrawdir + PATHSEP + searchpath[i] + PATHSEP + realpart;
	    if (fileExists(realfile)) {
		found = true;
		isPart = (i == 1);
		break;
	    }
	}
    }

    // Look in current directory
    if (!found) {
	realfile = realpart;
	found = fileExists(realfile);
    }
    if (!found) {
	cerr << "Unable to open file for part: " << realpart << '\n';
	return;
    }

    out << "AttributeBegin\n";
#if 0
    if (isPart) {
	out << "Scale 0.95 0.95 0.95\n";
    }
#endif
    out << "Attribute \"identifier\" \"string name\" [\"" << realpart << "\"]\n";
    out << "IfBegin \"$user:l2ribPass == 'main'\"\n";
    writeColour(out, colour);
    out << "ElseIf \"$user:l2ribPass == 'shadow'\"\n";
    out << "Surface \"null\"\n";
    out << "IfEnd\n";
    writeMatrix(out, matrix);

    if (isMPD) {
	string ribname = partname;
	ribname.replace(ribname.length() - 3, 3, "rib");

	// Look for the MPD part bound. If the MPD part bound is
	// itself incomplete, we'll have to set our own bound to
	// incomplete as well.
	hash_map<string,Bound>::iterator mBi = mpdBounds.find(ribname);
	if (mBi != mpdBounds.end()) {
	    Bound& mpdbound = mBi->second;
	    if (!mpdbound.init || mpdbound.mpdincomplete) {
		bound.mpdincomplete = true;
		out << "ReadArchive \"" << ribname << "\"\n";		
	    } else {
		if (doDRA) {
		    out << "Procedural \"DelayedReadArchive\" [\"" << ribname << "\"] [" << mpdbound << "]\n";
		} else {
		    out << "ReadArchive \"" << ribname << "\"\n";
		}
		bound.expand(mpdbound, matrix);		
	    }
	} else {
	    // Can't find it at all. Probably a forward part reference
	    bound.mpdincomplete = true;
	    out << "ReadArchive \"" << ribname << "\"\n";
	}
    }
    else {
	Bound newbound;
	ifstream in(realfile.c_str());
	if (!in) {
	    cerr << "Unable to open file: " << realfile << '\n';
	    return;
	}
	parseFile(out, in, realpart, newbound);

	// The newbound needs to be transformed by the matrix, then
	// expands the old bound. Thankfully, the only time we need
	// to do a matrix multiply.
	bound.expand(newbound, matrix);
    }

    out << "AttributeEnd\n";
}

void drawBound(ostream &out, const string& colour, Bound &bound) {
    out << "AttributeBegin\n";
    // Lines should never be visible to raytracing
    out << "Attribute \"visibility\" \"int trace\" [0] \"string transmission\" [\"transparent\"]\n";
    writeColour(out, colour);
    out << "Procedural \"DynamicLoad\" [\"line.rll\" \"12 1 ";
    out << bound.minx << ' ' << bound.miny << ' ' << bound.minz << ' '
	<< bound.minx << ' ' << bound.maxy << ' ' << bound.minz << ' '
	<< bound.minx << ' ' << bound.maxy << ' ' << bound.minz << ' '
	<< bound.maxx << ' ' << bound.maxy << ' ' << bound.minz << ' '
	<< bound.maxx << ' ' << bound.maxy << ' ' << bound.minz << ' '
	<< bound.maxx << ' ' << bound.miny << ' ' << bound.minz << ' '
	<< bound.maxx << ' ' << bound.miny << ' ' << bound.minz << ' '
	<< bound.minx << ' ' << bound.miny << ' ' << bound.minz << ' '
	<< bound.minx << ' ' << bound.miny << ' ' << bound.maxz << ' '
	<< bound.minx << ' ' << bound.maxy << ' ' << bound.maxz << ' '
	<< bound.minx << ' ' << bound.maxy << ' ' << bound.maxz << ' '
	<< bound.maxx << ' ' << bound.maxy << ' ' << bound.maxz << ' '
	<< bound.maxx << ' ' << bound.maxy << ' ' << bound.maxz << ' '
	<< bound.maxx << ' ' << bound.miny << ' ' << bound.maxz << ' '
	<< bound.maxx << ' ' << bound.miny << ' ' << bound.maxz << ' '
	<< bound.minx << ' ' << bound.miny << ' ' << bound.maxz << ' '
	<< bound.minx << ' ' << bound.miny << ' ' << bound.minz << ' '
	<< bound.minx << ' ' << bound.miny << ' ' << bound.maxz << ' '
	<< bound.maxx << ' ' << bound.miny << ' ' << bound.minz << ' '
	<< bound.maxx << ' ' << bound.miny << ' ' << bound.maxz << ' '
	<< bound.minx << ' ' << bound.maxy << ' ' << bound.minz << ' '
	<< bound.minx << ' ' << bound.maxy << ' ' << bound.maxz << ' '
	<< bound.maxx << ' ' << bound.maxy << ' ' << bound.minz << ' '
	<< bound.maxx << ' ' << bound.maxy << ' ' << bound.maxz;
    out << "\"] [ " << bound << "]\n";
    out << "AttributeEnd\n";
}

void drawLines(ostream& out, const string& colour, vector<Point>& points, bool doOptionalLines) {
    Bound bound;
    out << "IfBegin \"$user:l2ribLines == 1\"\n";
    out << "AttributeBegin\n";
    out << "Attribute \"visibility\" \"int trace\" [0] \"string transmission\" [\"transparent\"]\n";
    writeColour(out, colour);
    if (doOptionalLines) {
	out << "Procedural \"DynamicLoad\" [\"line.rll\" \"" << (points.size() / 4)  << " 2";	
    } else {
	out << "Procedural \"DynamicLoad\" [\"line.rll\" \"" << (points.size() / 2)  << " 1";
    }
    for (vector<Point>::const_iterator i = points.begin(); i != points.end(); ++i) {
	bound.expand(*i);
	out << ' ' << *i;
    }
    out << "\"] [ " << bound << "]\n";
    out << "AttributeEnd\n";
    out << "IfEnd\n";
    points.clear();
}

void drawPolys(ostream& out, const string& colour, vector<int>& polySizes, vector<Point>& polyPoints) {
    int total = 0;

    out << "AttributeBegin\n";
    writeColour(out, colour);
    out << "PointsPolygons [";
    vector<Point>::const_iterator k = polyPoints.begin();
    for (vector<int>::const_iterator i = polySizes.begin(); i != polySizes.end(); ++i) {
	out << ' ' << *i;
	total += *i;
    }
    out << "]\n[";
    for (int j = 0; j < total; ++j) {
	out << ' ' << j;
    }
    out << "]\n\"P\" [";
    for (k = polyPoints.begin(); k != polyPoints.end(); ++k) {
	out << ' ' << *k;
    }
    out << "]\n";
    out << "AttributeEnd\n";
    polySizes.clear();
    polyPoints.clear();
}

bool isBowtie(const Point& p1, const Point& p2, const Point& p3, const Point& p4) {

    // This is not the most optimal algorithm.
    
    // Assume that we're somewhat coplanar, and not colinear. First,
    // figure out the plane which goes through the first 3 points.

    // v1 = normalize(p2 - p1)
    Point v1 = p2 - p1;
    float length = sqrt(v1.x * v1.x + v1.y * v1.y + v1.z * v1.z);
    v1.x /= length; v1.y /= length; v1.z /= length;

    // v2 = normalize(p3 - p1)
    Point v2 = p3 - p1;
    length = sqrt(v2.x * v2.x + v2.y * v2.y + v2.z * v2.z);
    v2.x /= length; v2.y /= length; v2.z /= length;
    
    // n = v1 x v2 (n of the plane going through p1, p2, p3)
    Point n(v1.y*v2.z - v1.z*v2.y, v1.z*v2.x - v1.x*v2.z, v1.x*v2.y - v1.y*v2.x);

    // Project p4 - p0 onto the normal vector starting at p0 and add
    // from p4 in order to project p4 onto the plane to get 4 fully
    // coplanar points
    float dotP = (p4.x - p1.x) * n.x + (p4.y - p1.y) * n.y + (p4.z - p1.z) * n.z;
    Point p4p(p4.x - dotP * n.x, p4.y - dotP * n.y, p4.z - dotP * n.z);

    // Find plane going through p1 and p3 perpendicular to original
    // plane..
    Point n2(n.y*v2.z - n.z*v2.y, n.z*v2.x - n.x*v2.z, n.x*v2.y - n.y*v2.x);
    float d = n2.x * p1.x + n2.y * p1.y + n2.z * p1.z; 

    // .. and finally check whether p2 and p4 are on same side of this
    // new plane; if so we are a bowtie quad.
    return (((n2.x * p2.x + n2.y * p2.y + n2.z * p2.z) > d) == ((n2.x * p4p.x + n2.y * p4p.y + n2.z * p4p.z) > d));
}

void mpdScan(ifstream &in) {
    string line;
    while (in && in.peek() != EOF) {
	getline(in, line);
	string token = tokenize(line);

	if (!token.empty() && isNumericString(token) && atoi(token.c_str()) == 0) {
	    token = tokenize(line);
	    if (token == "FILE") {
		if (!doMPD) {
		    // This is the first FILE command
		    // encountered. Ignore the command because it'll
		    // be the main file, but set our MPD processing
		    // flag
		    doMPD = true;
		} else {
		    // Add the file name to the list
		    string filename = fixFileName(line);
		    mpdNames.insert(filename);
		}
	    }
	}
    }
}

void mpdSkipFirstFile(ifstream &in) {
    string line;
    while (in && in.peek() != EOF) {
	getline(in, line);
	string token = tokenize(line);

	if (!token.empty() && isNumericString(token) && atoi(token.c_str()) == 0) {
	    token = tokenize(line);
	    if (token == "FILE")
		return;
	}
    }
}

string mpdGetNextFileName(ifstream &in) {
    string line;
    while (in && in.peek() != EOF) {
	getline(in, line);
	string token = tokenize(line);

	if (!token.empty() && isNumericString(token) && atoi(token.c_str()) == 0) {
	    token = tokenize(line);
	    if (token == "FILE") {
		string filename = fixFileName(line);
		filename.replace(filename.length() -3, 3, "rib");
		return filename;
	    }
	}
    }
    return "";
}

bool parseFile(ostream &out, ifstream &in, const string& partname, Bound& bound) {
    if (!partname.empty()) {
	string ribname = partname;
	ribname.replace(ribname.length() - 3, 3, "rib");

	// Check whether a prebuilt rib exists
	string pfilename = l2ribdir + PATHSEP + "prebuilt" + PATHSEP + ribname;
	if (fileExists(pfilename)) {
	    getBound(bound, pfilename, partname);
	    if (doDRA) {
		out << "Procedural \"DelayedReadArchive\" [\"" << fixRIBFileName(ribname) << "\"] [" << bound << "]\n";
	    } else {
		out << "ReadArchive \"" << fixRIBFileName(ribname) << "\"\n";
	    }
	    return true;
	}

	// Check whether a cached rib exists
	string ofilename = cachedir + PATHSEP + ribname;

	// Note this call may check the datestamp on the RIB file
	if (fileExists(ofilename, !usecache)) {
	    getBound(bound, ofilename, partname);
	    if (doDRA) {
		out << "Procedural \"DelayedReadArchive\" [\"" << fixRIBFileName(ribname) << "\"] [" << bound << "]\n";
	    } else {
		out << "ReadArchive \"" << fixRIBFileName(ribname) << "\"\n";
	    }		
	    return true;
	}

    }

    // Internal buffers
    ostringstream ostr;
    vector<int> polySizes;
    vector<Point> polyPoints;
    vector<Point> linePoints;
    bool doOptionalLines = false;
    string lastLineColour = "";
    string lastPolyColour = "";
    int i;
    
    string line;
    while (in && in.peek() != EOF) {
	int curpos = in.tellg();
	getline(in, line);
	string token = tokenize(line);

	if (!token.empty() && isNumericString(token)) {
	    switch(atoi(token.c_str())) {
		case 0:
		{
		    // comments, some of which masquerade as commands
		    // we might be interested in.
		    string command = tokenize(line);
		    // MPD extension. The outer loop handles the real logic
		    if (command == "FILE") {
			// Rewind the file
			in.seekg(curpos, ios::beg);
			// And finish it up
			goto endfile;
		    }
		    // Colour codes
		    else if (command == "!COLOUR") {
			parseColour(line);
		    }
		    // Write/print
		    else if (command == "WRITE" || command == "PRINT") {
			out << "# " << line << "\n";
		    }
		    break;
		}
		case 1:
		{
		    // Flush line buffer
		    if (!linePoints.empty()) {
			drawLines(ostr, lastLineColour, linePoints, doOptionalLines);
			lastLineColour = "";
		    }
		    // Flush poly buffer
		    if (!polySizes.empty()) {
			drawPolys(ostr, lastPolyColour, polySizes, polyPoints);
			lastPolyColour = "";
		    }
		    // Part insertion
		    string colour;
		    float matrix[16];
		    string partname;
		    matrix[3] = matrix[7] = matrix[11] = 0;
		    matrix[15] = 1;

		    istringstream lineStream(line.c_str());
		    lineStream >> colour >> matrix[12] >> matrix[13] >> matrix[14] >> matrix[0] >> matrix[4] >> matrix[8] >> matrix[1] >> matrix[5] >> matrix[9] >> matrix[2] >> matrix[6] >> matrix[10];
                    getline(lineStream, partname);
                    REMOVE_SPACES(partname);

		    // Zero scales aren't handled very well,
		    // particularly during ray tracing. So we need to
		    // fudge the scales a bit if this comes up.
		    if (matrix[0] == 0) matrix[0] = 0.001;
		    if (matrix[5] == 0) matrix[5] = 0.001;
		    if (matrix[10] == 0) matrix[10] = 0.001;

		    partname = fixFileName(partname);
		    insertPart(ostr, colour, matrix, partname, bound);
		    break;
		}

		case 2:
		{
		    // line
		    string colour;
		    Point p[2];
		    istringstream lineStream(line.c_str());
		    lineStream >> colour >> p[0] >> p[1];

		    // Were we just drawing optional lines? Is this
		    // line a new colour? In either case, flush the
		    // line buffer
		    if ((doOptionalLines || (lastLineColour != "" && colour != lastLineColour)) && !linePoints.empty()) {
			drawLines(ostr, lastLineColour, linePoints, doOptionalLines);
		    }
		    doOptionalLines = false;
		    lastLineColour = colour;
		    // Push new line into buffer and expand bounds of
		    // this file
		    for (i = 0; i < 2; ++i) {
			linePoints.push_back(p[i]);
			bound.expand(p[i]);
		    }
		    break;
		}
		case 3:
		{
		    string colour;
		    Point p[3];
		    istringstream lineStream(line.c_str());
		    lineStream >> colour >> p[0] >> p[1] >> p[2];

		    bound.expand(p[0]);
		    bound.expand(p[1]);
		    bound.expand(p[2]);

		    // Is this poly a new colour? If so flush poly
		    // buffer
		    if (lastPolyColour != "" && colour != lastPolyColour && !polySizes.empty()) {
			drawPolys(ostr, lastPolyColour, polySizes, polyPoints);
		    }
		    lastPolyColour = colour;
		    // Push poly into buffer
		    polySizes.push_back(3);
		    for (i = 0; i < 3; ++i) {
			polyPoints.push_back(p[i]);
		    }
		    break;
		}

		case 4:
		{
		    string colour;
		    Point p[4];
		    istringstream lineStream(line.c_str());

		    lineStream >> colour >> p[0] >> p[1] >> p[2] >> p[3];

		    bound.expand(p[0]);
		    bound.expand(p[1]);
		    bound.expand(p[2]);
		    bound.expand(p[3]);

		    // Is this poly a new colour? If so, flush poly
		    // buffer
		    if (lastPolyColour != "" && colour != lastPolyColour && !polySizes.empty()) {
			drawPolys(ostr, lastPolyColour, polySizes, polyPoints);
		    }
		    lastPolyColour = colour;

		    // Push poly into buffer, compensating for bowtie
		    // quads
		    polySizes.push_back(4);
		    if (!isBowtie(p[0], p[1], p[2], p[3])) {
			polyPoints.push_back(p[0]);
			polyPoints.push_back(p[1]);
			polyPoints.push_back(p[2]);
			polyPoints.push_back(p[3]);
		    } else {
			polyPoints.push_back(p[0]);
			polyPoints.push_back(p[1]);
			polyPoints.push_back(p[3]);
			polyPoints.push_back(p[2]);
		    }
		    break;
		}
		case 5:
		{
		    // optional line
		    string colour;
		    Point p[4];
		    istringstream lineStream(line.c_str());
		    lineStream >> colour >> p[0] >> p[1] >> p[2] >> p[3];

		    // Were we just drawing non-optional lines? Is
		    // this line a new colour? In either case,
		    // flush the line buffer
		    if ((!doOptionalLines || (lastLineColour != "" && colour != lastLineColour)) && !linePoints.empty()) {
			drawLines(ostr, lastLineColour, linePoints, doOptionalLines);
		    }
		    doOptionalLines = true;
		    lastLineColour = colour;
		    // Push new line into buffer and expand the
		    // bounds of this file
		    for (i = 0; i < 4; ++i) {
			bound.expand(p[i]);
			linePoints.push_back(p[i]);
		    }
		    break;
		}
	    }
	}
    }

    endfile:
    // Flush buffers one last time
    if (!linePoints.empty()) {
	drawLines(ostr, lastLineColour, linePoints, doOptionalLines);
	lastLineColour = "";
    }
    if (!polySizes.empty()) {
	drawPolys(ostr, lastPolyColour, polySizes, polyPoints);
    }

    // Store bound
    boundsMap[partname] = bound;

    if (!partname.empty()) {
	
	string ribname = partname;
	ribname.replace(ribname.length() - 3, 3, "rib");

	// Make sure the directory exists for the part, and try to
	// create it if it doesn't
	string::size_type slash = partname.rfind(PATHSEP);
	if (slash != string::npos) {
	    string partdir = partname.substr(0, slash);
	    string ofiledir = cachedir + PATHSEP + partdir;
	    struct stat statbuf;
	    if (stat(ofiledir.c_str(), &statbuf) == -1 || !(statbuf.st_mode & S_IFDIR)) {
		// attempt to create the directory
#ifdef _WIN32
		if (mkdir(ofiledir.c_str()) == -1) {
#else
		if (mkdir(ofiledir.c_str(), 0777) == -1) {
#endif
		    cerr << "Unable to open file directory for writing: " << ofiledir << '\n';
		    return false;
		}
	    }
	    
	}
	// Write to archive rib if we need to..
	string ofilename = cachedir + PATHSEP + ribname;
	ofstream ofile(ofilename.c_str());
	if (!ofile) {
	    cerr << "Unable to open file for writing: " << ofilename << '\n';
	    return false;
	}
	if (bound.init) {
	    ofile << "Bound " << bound << '\n';
	}
	ofile << ostr.str();
	out << "Procedural \"DelayedReadArchive\" [\"" << fixRIBFileName(ribname) << "\"] [" << bound << "]\n";
    } else {
	// Write directly to upper stream
	if (bound.init) {
	    out << "Bound " << bound << '\n';
	}
	out << ostr.str();
    }
    return true;
}


//////////////////////////////////////////////////
// Main program
//////////////////////////////////////////////////

string getConfigFile(void) {
#ifdef _WIN32
    char path[MAX_PATH];

    // SHGetFolderPath is in the Platform SDK, and can be found in
    // ShFolder.dll. It should be safe for all Windows platforms.
    if (SUCCEEDED(SHGetFolderPath(0, CSIDL_APPDATA|CSIDL_FLAG_CREATE, 0, 0, path))) 
    {
	// I don't think PathAppend is entirely safe?
	sprintf(path, "%s%s%s", path, PATHSEP, "l2rib");
	if (GetFileAttributes(path) == 0xFFFFFFFF) {
	    if (GetLastError() == ERROR_FILE_NOT_FOUND) {
		cerr << path << " directory not found, creating.\n";
		if (!SUCCEEDED(CreateDirectory(path, 0))) {
		    cerr << "Unable to create directory " << path << ", aborting.\n";
		    exit(1);
		}
	    } else {
		cerr << "Unable to access " << path << ", aborting.\n";
		exit(1);
	    }
	}
	return string(path) + PATHSEP + "l2rib.ini";
    } else {
	cerr << "Unable to determine common common application data directory.\n";
	cerr << "Cannot create l2rib.ini, aborting.\n";	
	exit(1);
    }
#else
    if (!getenv("HOME")) {
	cerr << "HOME environment variable not set, unable to continue.\n";
	exit(1);
    }
    return string(getenv("HOME")) + "/.l2ribrc";
#endif
}

void interactiveConfig(void) {
    cout << "Enter LDraw directory (parts are under this folder):\n";
    cin >> ldrawdir;
    
    cout << "Enter l2rib installation directory:\n";
    cin >> l2ribdir;

    cout << "Enter temporary cache file directory:\n";
    cin >> cachedir;

    string filename = getConfigFile();
    ofstream out(filename.c_str());
    if (!out) {
	cerr << "Unable to open configuration file for writing.\nPlease check permissions on " + filename + "\n";
	exit(1);
    }
    out << "ldrawdir=" << ldrawdir << '\n';
    out << "l2ribdir=" << l2ribdir << '\n';
    out << "cachedir=" << cachedir << '\n';
    cout << "Configuration complete.\n";
}

void readConfig(void) {
    string filename = getConfigFile();    
    ifstream in(filename.c_str());
    if (!in) {
	cerr << "Configuration file not found - running initial configuration.\n";
	interactiveConfig();
	exit(0);
    }
    string line;
    while (in && in.peek() != EOF) {
	getline(in, line);
	if (line != "" && line[0] != '#') {
	    string::size_type index = line.find('=');
	    if (index != string::npos) {
		string key(line, 0, index);
		string value(line, index + 1, line.length());
		if (key == "bgcolor") {
		    bgcolor[0] = atof(tokenize(value).c_str());
		    bgcolor[1] = atof(tokenize(value).c_str());
		    bgcolor[2] = atof(tokenize(value).c_str());
		} else if (key == "cachedir") {
		    cachedir = value;
		} else if (key == "camerafrom") {
		    cameraFrom.x = atof(tokenize(value).c_str());
		    cameraFrom.y = atof(tokenize(value).c_str());
		    cameraFrom.z = atof(tokenize(value).c_str());
		} else if (key == "camerato") {
		    cameraTo.x = atof(tokenize(value).c_str());
		    cameraTo.y = atof(tokenize(value).c_str());
		    cameraTo.z = atof(tokenize(value).c_str());
		} else if (key == "cameraup") {
		    cameraUp.x = atof(tokenize(value).c_str());
		    cameraUp.y = atof(tokenize(value).c_str());
		    cameraUp.z = atof(tokenize(value).c_str());
		} else if (key == "cameradistance") {
		    camDistance = atof(value.c_str());
		} else if (key == "floor") {
		    floorScale = atof(value.c_str());
		} else if (key == "format") {
		    formatX = atoi(value.c_str());
		    formatY = atoi(value.c_str());
		} else if (key == "l2ribdir") {
		    l2ribdir = value;
		} else if (key == "ldrawdir") {
		    ldrawdir = value;
		} else if (key == "light") {
		    Point p;
		    p.x = atof(tokenize(value).c_str());
		    p.y = atof(tokenize(value).c_str());
		    p.z = atof(tokenize(value).c_str());
		    lightPositions.push_back(p);
		    lightColours.push_back(atof(tokenize(value).c_str()));
		    lightColours.push_back(atof(tokenize(value).c_str()));
		    lightColours.push_back(atof(tokenize(value).c_str()));
		    lightIntensities.push_back(atof(tokenize(value).c_str()));
		    lightShadowTypes.push_back(tokenize(value));
		} else if (key == "pixelsamples") {
		    pixelSamples = atoi(value.c_str());
		} else if (key == "raytrace") {
		    doRaytrace = true;
		} else if (key == "shadingrate") {
		    shadingRate = atof(value.c_str());
		} else if (key == "shadowformat") {
		    shadowFormat = atoi(value.c_str());
		}
	    }
	}
    }    
}

void usage(const string& name) {
    cerr << "Usage: " << name << " [options] file\n"
         << "Options:\n"
         << " -bgcolor r g b            Set background color\n"
         << " -camerafrom x y z         Set camera position\n"
         << " -camerato x y z           Set camera target\n"
         << " -cameraup x y z           Set camera up vector\n"
         << " -cameradistance scale     Set camera distance multiplier\n"
         << " -colorconfig file         Set color configuration file\n"
         << " -file                     Render to TIFF file instead of framebuffer\n"
         << " -floor scale              Set floor size multiplier\n"
         << " -format x y               Size of render\n"
         << " -light x y z r g b i mode Add light source at (x y z) with color (r g b),\n"
         << "                            intensity i and shadow mode (one of map,\n"
         << "                            cache, none, or raytrace)\n"
         << " -lines                    Draw lines\n"
	 << " -nocache                  Ignore previously cached RIB files\n"
         << " -o                        Use specified output file instead of stdout\n"
         << " -pixelsamples s           Set pixel samples\n"
         << " -raytrace                 Output proper raytrace visibility\n"
         << " -shadingrate r            Set shading rate\n"
         << " -shadowformat size        Size of shadow maps\n"
         << " -studlogo                 Output studs with displaced logo\n";
}

// This assumes a left hand coordinate system!
void camLookAt(ostream& out, const Point& from, const Point& to, const Point& up) {

    // view direction vector
    Point view(to.x - from.x, to.y - from.y, to.z - from.z);
    float length = sqrt(view.x * view.x + view.y * view.y + view.z * view.z);
    view.x /= length; view.y /= length; view.z /= length;

    // x axis vector - view cross up
    Point axisx(view.y*up.z - view.z*up.y, view.z*up.x - view.x*up.z, view.x*up.y - view.y*up.x);
    length = sqrt(axisx.x * axisx.x + axisx.y * axisx.y + axisx.z * axisx.z);
    axisx.x /= length; axisx.y /= length; axisx.z /= length;

    // new up vector - view cross x axis
    Point nup (axisx.y*view.z - axisx.z*view.y, axisx.z*view.x - axisx.x*view.z, axisx.x*view.y - axisx.y*view.x);

    out << "ConcatTransform ["
	<< axisx.x << ' ' << nup.x << ' ' << view.x << " 0 "
	<< axisx.y << ' ' << nup.y << ' ' << view.y << " 0 "
	<< axisx.z << ' ' << nup.z << ' ' << view.z << " 0 "
	<< "0 0 0 1]\n";
}

int main(int argc, char*argv[]) {

    programTime = time(0);
    
    readConfig();

    string filename = "";
    string ofilename = "";

    bool useFile = false;
    int i, j;
    
    for (i = 1; i < argc; ++i) {
	if (strlen(argv[i]) > 1 && argv[i][0] == '-') {
	    string opt(argv[i] + 1);

	    if (opt == "bgcolor") {
		if (i + 3 >= argc ||
		    !isFloatString(string(argv[i+1])) ||
		    !isFloatString(string(argv[i+2])) ||
		    !isFloatString(string(argv[i+3]))) {
		    cerr << "Expecting three numeric values after -bgcolor.\n";
		    return 1;
		}
		bgcolor[0] = atof(argv[i+1]);
		bgcolor[1] = atof(argv[i+2]);
		bgcolor[2] = atof(argv[i+3]);
		i+=3;
	    } else if (opt == "camerafrom") {
		if (i + 3 >= argc ||
		    !isFloatString(string(argv[i+1])) ||
		    !isFloatString(string(argv[i+2])) ||
		    !isFloatString(string(argv[i+3]))) {
		    cerr << "Expecting three numeric values after -camerafrom.\n";
		    return 1;
		}
		cameraFrom.x = atof(argv[i+1]);
		cameraFrom.y = atof(argv[i+2]);
		cameraFrom.z = atof(argv[i+3]);
		i+=3;
	    } else if (opt == "camerato") {
		if (i + 3 >= argc ||
		    !isFloatString(string(argv[i+1])) ||
		    !isFloatString(string(argv[i+2])) ||
		    !isFloatString(string(argv[i+3]))) {
		    cerr << "Expecting three numeric values after -camerato.\n";
		    return 1;
		}
		cameraTo.x = atof(argv[i+1]);
		cameraTo.y = atof(argv[i+2]);
		cameraTo.z = atof(argv[i+3]);
		i+=3;
	    } else if (opt == "cameraup") {
		if (i + 3 >= argc ||
		    !isFloatString(string(argv[i+1])) ||
		    !isFloatString(string(argv[i+2])) ||
		    !isFloatString(string(argv[i+3]))) {
		    cerr << "Expecting three numeric values after -cameraup.\n";
		    return 1;
		}
		cameraUp.x = atof(argv[i+1]);
		cameraUp.y = atof(argv[i+2]);
		cameraUp.z = atof(argv[i+3]);
		i+=3;
	    } else if (opt == "cameradistance") {
		++i;
		if (i == argc || !isFloatString(string(argv[i]))) {
		    cerr << "Expecting numeric value after -cameradistance.\n";
		    return 1;
		}
		camDistance = atof(argv[i]);
	    } else if (opt == "colorconfig") {
		++i;
		if (i == argc) {
		    cerr << "Expecting color configuration file after -colorconfig.\n";
		    return 1;
		}
		colorcfg = argv[i];
	    } else if (opt == "file") {
		useFile = true;
	    } else if (opt == "floor") {
		++i;
		if (i == argc || !isFloatString(string(argv[i]))) {
		    cerr << "Expecting numeric value after -floor.\n";
		    return 1;
		}
		floorScale = atof(argv[i]);
	    } else if (opt == "format") {
		if (i + 2 >= argc || 
		    !isNumericString(string(argv[i+1])) ||
		    !isNumericString(string(argv[i+2]))) {
		    cerr << "Expecting two numeric values after -format.\n";
		    return 1;
		}
		formatX = atoi(argv[i+1]);
		formatY = atoi(argv[i+2]);
		i += 2;
	    } else if (opt == "light") {
		if (i + 8 >= argc ||
		    !isFloatString(string(argv[i+1])) ||
		    !isFloatString(string(argv[i+2])) ||
		    !isFloatString(string(argv[i+3])) ||
		    !isFloatString(string(argv[i+4])) ||
		    !isFloatString(string(argv[i+5])) ||
		    !isFloatString(string(argv[i+6])) ||
		    !isFloatString(string(argv[i+7]))) {
		    cerr << "Expecting position (3 floats), color (3 floats), intensity (float), and\nshadow type (none, map, cache, or raytrace) after -light.\n";
		    return 1;
		}
		string type(argv[i+8]);
		if (type == "raytrace" || type == "map" || type == "none" || type == "cache") {
		    lightShadowTypes.push_back(type);
		} else {
		    cerr << "Invalid shadow type - must be none, map, cache, or raytrace.\n";
		    return 1;
		}

		Point p;
		p.x = atof(argv[i+1]);
		p.y = atof(argv[i+2]);
		p.z = atof(argv[i+3]);
		lightPositions.push_back(p);
		lightColours.push_back(atof(argv[i+4]));
		lightColours.push_back(atof(argv[i+5]));
		lightColours.push_back(atof(argv[i+6]));
		lightIntensities.push_back(atof(argv[i+7]));
		i+=8;
	    } else if (opt == "lines") {
		doLines = true;
	    } else if (opt == "nocache") {
		usecache = false;
	    } else if (opt == "o") {
		if (!ofilename.empty()) {
		    cerr << "Multiple output filenames specified.\n";
		    return 1;
		}
		++i;
		if (i == argc) {
		    cerr << "Expecting output filename after -o.\n";
		    return 1;
		}
		ofilename = argv[i];
	    } else if (opt == "pixelsamples") {
		++i;
		if (i == argc || !isFloatString(string(argv[i]))) {
		    cerr << "Expecting numeric value after -pixelsamples.\n";
		    return 1;
		}
		pixelSamples = atoi(argv[i]);
	    } else if (opt == "raytrace") {
		doRaytrace = true;
	    } else if (opt == "shadingrate") {
		++i;
		if (i == argc || !isFloatString(string(argv[i]))) {
		    cerr << "Expecting numeric value after -shadingrate.\n";
		    return 1;
		}
		shadingRate = atof(argv[i]);
	    } else if (opt == "shadowformat") {
		++i;
		if (i == argc || !isNumericString(string(argv[i]))) {
		    cerr << "Expecting numeric value after -shadowformat.\n";
		    return 1;
		}
		shadowFormat = atoi(argv[i]);
	    } else if (opt == "studlogo") {
		doStudLogo = true;
	    } else {
		cerr << "Unknown option: " << opt << '\n';
		usage(string(argv[0]));
		return 1;
	    }
	} else if (i == argc - 1) {
	    filename = argv[i];
	} else {
	    cerr << "Bad argument: " << argv[i] << '\n';
	    usage(string(argv[0]));
	    return 1;
	}
    }
    if (filename.empty()) {
	cerr << "No input file specified.\n";
	usage(string(argv[0]));
	return 1;
    }
    if (!fileExists(filename)) {
	cerr << filename << ": No such file or directory.\n";
	return 1;
    }
    // Default output is stdout, but use filename if specified
    ostream* out = &cout;
    if (!ofilename.empty()) {
	out = new ofstream(ofilename.c_str());
	if (!*out) {
	    delete(out);
	    cerr << "Unable to open output file \"" << ofilename << "\n";
	    return 1;
	}
    }

    // Install default lights if none specified
    if (lightPositions.empty()) {
	lightPositions.push_back(Point(-1, -1, -1));
	lightColours.push_back(1);
	lightColours.push_back(1);
	lightColours.push_back(1);
	lightIntensities.push_back(1);
	lightShadowTypes.push_back("none");
    }

    Bound bound;
    
    // Parse color definitions
    if (colorcfg.empty()) {
	colorcfg = ldrawdir + PATHSEP + "ldconfig.ldr";
    }
    ifstream colorin(colorcfg.c_str());
    if (colorin) {
	ostringstream colorout;	// just ignored
	parseFile(colorout, colorin, "", bound);

	// Fix any edgecolors that refer to codes
	for (i = 0; i < 512; ++i) {
	    if (ColourCodes[i].init && ColourCodes[i].edgecode > -1) {
		ColourCodes[i].edger = ColourCodes[ColourCodes[i].edgecode].r;
		ColourCodes[i].edgeg = ColourCodes[ColourCodes[i].edgecode].g;
		ColourCodes[i].edgeb = ColourCodes[ColourCodes[i].edgecode].b;
		ColourCodes[i].edgecode = -1;
	    }
	}
    } else {
	cerr << "Warning: unable to find color configuration file " << colorcfg << ". Colors may be wrong in output.\n";
    }
    
    
    // Do the work now and store in a temporary stream because we need
    // to precompute the bounds
    ifstream in(filename.c_str());
    if (!in) {
	cerr << "Unable to open file: " << filename << '\n';
	return 1;
    }

    // Scan for MPD filenames
    mpdScan(in);
    in.clear();
    in.seekg(0, ios::beg);

    // MPD processing. Skip pass the first 0 FILE
    if (doMPD) {
	mpdSkipFirstFile(in);
    }
    
    // Parse the input file. In the MPD case, this will parse the main
    // model only
    ostringstream ostr;
    parseFile(ostr, in, "", bound);

    // For MPD files, process the rest of the pieces. We do this
    // multiple times until we have enough information to fully
    // resolve bounding boxes.  (FIXME: if a part is missing in a MPD
    // file I suppose this could loop infinitely. Hmm.)
    if (doMPD) {
	bool mpdIncompleteBounds;
	do {
	    mpdIncompleteBounds = false;
	    while (1) {
		string partfilename = mpdGetNextFileName(in);
		if (partfilename.empty()) {
		    break;
		}
		ofstream* partfileout = new ofstream(partfilename.c_str());
		if (!*partfileout) {
		    delete(partfileout);
		    cerr << "Unable to open MPD output file \"" << partfilename << "\n";
		    continue;
		}
		Bound mpdbound;
		parseFile(*partfileout, in, "", mpdbound);
		partfileout->flush();
		partfileout->close();
		mpdBounds[partfilename] = mpdbound;
		delete partfileout;
	    }
	    bound.init = false;
	    bound.mpdincomplete = false;
	    in.clear();
	    in.seekg(0, ios::beg);
	    mpdSkipFirstFile(in);
	    ostr.clear();
	    parseFile(ostr, in, "", bound);
	} while (bound.mpdincomplete);
    }

    float distance = sqrt((bound.maxx - bound.minx) * (bound.maxx - bound.minx) + (bound.maxy - bound.miny) * (bound.maxy - bound.miny) + (bound.maxz - bound.minz) * (bound.maxz - bound.minz));

    *out << "##RenderMan RIB-Structure 1.1\n";
    *out << "##Creator l2rib v1.00 (http://www.levork.org/l2rib.html)\n";
    *out << "\n";
    *out << "##RenderMan RIB\n"; 
    *out << "version 3.04\n";
    *out << "Declare \"bias\" \"float\"\n";
    *out << "Option \"searchpath\" \"shader\" [\"" + fixRIBFileName(l2ribdir + PATHSEP + "shaders") + ":@\"]\n";
    *out << "Option \"searchpath\" \"archive\" [\"";
    *out << fixRIBFileName(l2ribdir + PATHSEP + "prebuilt") << ':' << fixRIBFileName(cachedir) << ":.\"]\n";
    *out << "Option \"shadow\" \"bias\" [" << distance * 0.01 << "]\n";
    *out << "Attribute \"trace\" \"bias\" [0.05]\n";

    if (doStudLogo) {
	*out << "Option \"user\" \"uniform int l2ribStudLogo\" [1]\n";
    } else {
	*out << "Option \"user\" \"uniform int l2ribStudLogo\" [0]\n";
    }

    // Shadow map passes
    for (i = 0; i < lightShadowTypes.size(); ++i) {
	if (lightShadowTypes[i] == "map") {
	    *out << "FrameBegin 1\n";
	    *out << "Option \"user\" \"uniform string l2ribPass\" \"shadow\"\n";
	    *out << "Option \"user\" \"uniform int l2ribLines\" [0]\n";
	    *out << "PixelSamples 2 2\n";
	    *out << "Format " << shadowFormat << ' ' << shadowFormat << " 1\n";
	    string shadowname = filename;
	    shadowname.replace(filename.length() - 3, 3, "light");
	    *out << "Display \"" << shadowname << i << ".tx\" \"shadow\" \"z\"\n";
	    *out << "Projection \"orthographic\"";
	    // Should be 0.5, but 0.55 gives us a little breathing roomx
	    *out << "ScreenWindow " << -distance * 0.55f << ' ' << distance * 0.55f << ' ' << -distance*0.55f << ' ' << distance*0.55f << '\n';
	    *out << "ShadingRate 1\n";

	    *out << "Identity\n";
	    *out << "Translate 0 0 " << distance << "\n";

	    camLookAt(*out, lightPositions[i], Point(0, 0, 0), Point(0, 1, 0));

	    *out << "WorldBegin\n";
	    *out << "Translate "
		 << (bound.maxx + bound.minx) * -0.5 << ' '
		 << (bound.maxy + bound.miny) * -0.5 << ' '
		 << (bound.maxz + bound.minz) * -0.5 << "\n";
	    *out << ostr.str();
	    *out << "WorldEnd\n";
	    *out << "FrameEnd\n";
	}
    }

    // Main map
    *out << "FrameBegin 1\n";
    *out << "Option \"user\" \"uniform string l2ribPass\" \"main\"\n";
    if (doLines) {
	*out << "Option \"user\" \"uniform int l2ribLines\" [1]\n";
    } else {
	*out << "Option \"user\" \"uniform int l2ribLines\" [0]\n";	
    }
    *out << "PixelSamples " << pixelSamples << ' ' << pixelSamples << '\n';
    *out << "Format " << formatX << ' ' << formatY << " 1\n";    
    *out << "ShadingRate " << shadingRate << '\n';
    if (useFile) {
	string tiffname = filename;
	tiffname.replace(filename.length() - 3, 3, "tif");
	*out << "Display \"" << fixRIBFileName(tiffname) << "\" \"tiff\" \"rgba\"\n";
    } else {
	*out << "Display \"" << fixRIBFileName(filename) << "\" \"framebuffer\" \"rgba\"\n";
    }
    *out << "Projection \"perspective\" \"fov\" [45]\n";
    *out << "Clipping 0.1 " << camDistance * distance + max(distance * 4, distance * floorScale * 4) << '\n';
    *out << "Identity\n";
    *out << "Translate 0 0 " << camDistance * distance << "\n";
    camLookAt(*out, cameraFrom, cameraTo, cameraUp);

    // Lights
    for (i = 0, j = 0; i < lightShadowTypes.size(); ++i, j+=3) {
	if (lightShadowTypes[i] == "none") {
	    *out << "LightSource \"distantlight\" \"distantlight" << i << "\" \"from\" [" << lightPositions[i] << "] \"to\" [0 0 0] \"intensity\" [" << lightIntensities[i] << "] \"lightcolor\" [" << lightColours[j] << ' ' << lightColours[j+1] << ' ' << lightColours[j+2] << "]\n";
	} else if (lightShadowTypes[i] == "raytrace") {
	    string shadowname = filename;
	    doRaytrace = true;
	    shadowname.replace(filename.length() - 3, 3, "light");
	    *out << "LightSource \"shadowdistant\" \"rtshadowdistant" << i << "\" \"from\" [" << lightPositions[i] << "] \"to\" [0 0 0] \"shadowname\" [\"raytrace\"] \"intensity\" [" << lightIntensities[i] << "] \"lightcolor\" [" << lightColours[j] << ' ' << lightColours[j+1] << ' ' << lightColours[j+2] << "]\n";
	} else if (lightShadowTypes[i] == "map" || lightShadowTypes[i] == "cache") {
	    string shadowname = filename;
	    shadowname.replace(filename.length() - 3, 3, "light");
	    *out << "LightSource \"shadowdistant\" \"shadowdistant" << i << "\" \"from\" [" << lightPositions[i] << "] \"to\" [0 0 0] \"shadowname\" [\"" << shadowname << i << ".tx\"] \"intensity\" [" << lightIntensities[i] << "] \"lightcolor\" [" << lightColours[j] << ' ' << lightColours[j+1] << ' ' << lightColours[j+2] << "]\n";
	}
    }
    *out << "Imager \"background\" \"background\" [" << bgcolor[0] << ' ' << bgcolor[1] << ' ' << bgcolor[2] << "]\n";
    *out << "WorldBegin\n";
    *out << "Translate "
	 << (bound.maxx + bound.minx) * -0.5 << ' '
	 << (bound.maxy + bound.miny) * -0.5 << ' '
	 << (bound.maxz + bound.minz) * -0.5 << "\n";
    if (floorScale) {
	*out << "AttributeBegin\n";
	*out << "Attribute \"identifier\" \"string name\" [\"l2ribfloor\"]\n";
	*out << "Color 0.5 0.5 0.5\n";
	*out << "Surface \"matte\"\n";
	*out << "Patch \"bilinear\" \"P\" [" <<
	    -distance * floorScale << ' ' << bound.maxy << ' ' << -distance * floorScale << ' ' <<
	     distance * floorScale << ' ' << bound.maxy << ' ' << -distance * floorScale << ' ' <<
	    -distance * floorScale << ' ' << bound.maxy << ' ' <<  distance * floorScale << ' ' <<
	     distance * floorScale << ' ' << bound.maxy << ' ' <<  distance * floorScale << ' ' << "]\n";
	*out << "AttributeEnd\n";
	
    }
    *out << "AttributeBegin\n";
    if (doRaytrace) {
	*out << "Attribute \"visibility\" \"int trace\" [1] \"string transmission\" [\"opaque\"]\n";
    }
    *out << "Color 1 1 1\n";
    *out << "Attribute \"user\" \"uniform color edgecolor\" [0 0 0]\n";
    *out << "Opacity 1 1 1\n";
    *out << "Surface \"plastic\" \"Ks\" [0.8]\n";

#if 0
    // Useful for debugging
    drawBound(*out, "0", bound);
#endif
    *out << ostr.str();
    *out << "AttributeEnd\n";
    *out << "WorldEnd\n";
    *out << "FrameEnd\n";

    if (!ofilename.empty()) {
	delete out;
    }
    return 0;

}
