#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <sstream>
#include <vector>

using namespace std;

inline bool caseInsCharCompareN(char a, char b) {
   return(toupper(a) == toupper(b));
}


/**
  Various useful static methods
  **/
class Util
{
public:



    /**
      Trims s on the right from any single character in delimiters
      **/
    static string RTrim(string s, string delimiters = " \f\n\r\t\v")
    {
        if (s.length() == 0)
            return s;

      return s.substr( 0, s.find_last_not_of( delimiters ) + 1 );
    }


    /**
      Trims s on the left from any single character in delimiters
      **/
    static string LTrim(string s, string delimiters = " \f\n\r\t\v" )
    {
      if (s.length() == 0)
        return s;

      return s.substr( s.find_first_not_of( delimiters ) );
    }

    /**
      Trims s on the left and right from any single character in delimiters
      **/
    static string Trim(std::string s, string delimiters = " \f\n\r\t\v" )
    {
      return LTrim( RTrim( s, delimiters ), delimiters );
    }


    /**
      Simply outputs str on stdout
      **/
    static void DebugOut(string str)
    {
        cout<<str<<endl;
    }

    /**
      Returns true if s1 = s2, false otherwise
      **/
    static bool Streq(const string& s1, const string& s2) {
       return((s1.size( ) == s2.size( )) &&
              equal(s1.begin( ), s1.end( ), s2.begin( ), caseInsCharCompareN));
    }

	
    /**
      Replaces all occurences of find by rep in str.
      **/
    static string ReplaceAll( const string& str, const string& find, const string& rep ) {
        if ( str.empty() || find.empty() || find == rep || str.find(find) == string::npos ) {
            return str;
        }
        ostringstream build_it;
        size_t i = 0;
        for ( size_t pos; ( pos = str.find( find, i ) ) != string::npos; ) {
            build_it.write( &str[i], pos - i );
            build_it << rep;
            i = pos + find.size();
        }
        if ( i != str.size() ) {
            build_it.write( &str[i], str.size() - i );
        }
        return build_it.str();

    }

	
	
    static string ToString(int v)
    {
        stringstream ss;
        ss << v;
        return ss.str();
    }

    static string ToString(double v)
    {
        stringstream ss;
        ss << v;
        return ss.str();
    }

    static double ToDouble(string s)
    {
        double d;
        stringstream ss;
        ss.str(s.c_str());
        ss >> d;

        return d;
    }

	static double ToFloat(string s)
	{
		float d;
		stringstream ss;
		ss.str(s.c_str());
		ss >> d;

		return d;
	}


    static bool IsDouble(string s)
    {
        //TODO : this might fail
        double d = 0;
        istringstream ss;
        ss.str(s.c_str());
        ss >> d;

        return (!ss.fail() && ss.eof());
    }


    static int ToInt(string s)
    {
        int i;
        stringstream ss;
        ss.str(s.c_str());
        ss >> i;

        return i;

    }


    /**
      Splits str by the splitter, returns a vector of all obtained strings
      */
    static vector<string> Split(string str, string splitter, bool includeEmpty = true)
    {
        vector<string> v;

        if (splitter.length() > str.length())
        {
            v.push_back(str);
            return v;
        }

        int current = 0;
        int next = -1 * splitter.length();


        do
        {
          current = next + splitter.length();
          next = str.find( splitter, current );

          if (next == string::npos)
          {
              if (includeEmpty || (current < str.length()))
              {
                v.push_back(str.substr( current ));
              }
          }
          else
          {
              if (includeEmpty || (next - current > 0))
              {
                v.push_back(str.substr( current, next - current ));
              }
          }
        }
        while (next != string::npos);

        return v;
    }


    /**
      Inserts splitter after each nbchars characters in str
      **/
    static string SplitByLength(string str, int nbchars, string splitter = "\n")
    {
        string out = "";

        int pos = 0;

        while (pos < str.length())
        {
            if (str.length() > pos + nbchars)
            {
                out += str.substr(pos, nbchars) + splitter;

            }
            else
            {
                out += str.substr(pos);
            }
            pos += nbchars;
        }

        return out;
    }

    static string ToLower(string str)
    {
        transform(str.begin(), str.end(),str.begin(), ::tolower );
        return str;
    }

    static string ToUpper(string str)
    {
        transform(str.begin(), str.end(),str.begin(), ::toupper );
        return str;
    }

    /**
      Just doubles every apostrophe in s, then wraps s with apostrophes
      **/
    static string DBEscape(string s)
    {
        return "'" + Util::ReplaceAll(s, "'", "''") + "'";
    }

    /**
      Prepares a list of values to be used in a query of the type
      "WHERE some_field IN (sz[0], sz[1], ...)"
      Usage :
      @code
      string q = "SELECT * FROM table WHERE field IN (" + Util::ToInstr(my_vector) + ")";
      @endcode
      **/
    static string ToInstr(vector<string> sz)
    {
        string instr = "";

        for (int i = 0; i < sz.size(); i++)
        {
            if (instr != "")
                instr +=",";
            instr += Util::DBEscape(sz[i]);
        }

        return instr;
    }



    static string GetSubstringBefore(string s, string separator)
    {
        int pos = s.find_first_of(separator);

        if (pos != string::npos)
            return s.substr(0, pos);

        return s;
    }

    static string GetSubstringAfter(string s, string separator)
    {
        int pos = s.find_last_of(separator);

        if (pos != string::npos)
            return s.substr(pos + 1);

        return s;
    }

	
	
	
	
    static string GetFileContent(string filename)
    {
        std::ifstream sifs(filename);
        std::string spcontent( (std::istreambuf_iterator<char>(sifs) ),
                             (std::istreambuf_iterator<char>()    ));
        sifs.close();

        return spcontent;
    }


    static void WriteFileContent(string filename, string content, bool append = false)
    {
        ofstream outfile;
        if (!append)
            outfile.open (filename);
        else
            outfile.open (filename, ios_base::app | ios_base::out);
        outfile<<content;
        outfile.close();
    }

    static string GetPathFilename(string fullpath)
    {
        return Util::GetSubstringAfter(fullpath, "/");
    }


    static string GetFileLine(string filename, int lineIndex)
    {
        ifstream file(filename);
        string line;
        int line_number = 0;

        string myLine = "";

        bool wereDone = false;
        while (std::getline(file, line) && !wereDone)
        {
            if (line_number == lineIndex)
            {
                 myLine = line;
                 wereDone = true;
            }
            line_number++;
        }

        return myLine;
    }
};

#endif // UTIL_H
