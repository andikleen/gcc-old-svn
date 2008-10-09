// 2003-12-19  Paolo Carlini  <pcarlini@suse.de>

// Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Free Software Foundation
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// 22.2.2.1.1  num_get members

#include <locale>
#include <sstream>
#include <testsuite_hooks.h>

void test01()
{
  using namespace std;
  typedef istreambuf_iterator<wchar_t> iterator_type;
  
  bool test __attribute__((unused)) = true;

  wistringstream iss;
  const num_get<wchar_t>& ng = use_facet<num_get<wchar_t> >(iss.getloc()); 
  ios_base::iostate err = ios_base::goodbit;
  iterator_type end;
  float f = 1.0f;
  double d = 1.0;
  long double ld = 1.0l;
  
  iss.str(L"1e.");
  err = ios_base::goodbit;
  end = ng.get(iss.rdbuf(), 0, iss, err, f);
  VERIFY( err == ios_base::failbit );
  VERIFY( *end == L'.' );
  VERIFY( f == 0.0f );

  iss.str(L"3e+");
  iss.clear();
  err = ios_base::goodbit;
  end = ng.get(iss.rdbuf(), 0, iss, err, d);
  VERIFY( err == (ios_base::failbit | ios_base::eofbit) );
  VERIFY( d == 0.0 );

  iss.str(L"6e ");
  iss.clear();
  err = ios_base::goodbit;
  end = ng.get(iss.rdbuf(), 0, iss, err, ld);
  VERIFY( *end == L' ' );

  // libstdc++/37624.  We can't always obtain the required behavior
  // when sscanf is involved, because of, e.g., glibc/1765.
#if defined(_GLIBCXX_HAVE_STRTOLD) && !defined(_GLIBCXX_HAVE_BROKEN_STRTOLD)
  VERIFY( err == ios_base::failbit );
  VERIFY( ld == 0.0l );
#endif
}

int main()
{
  test01();
  return 0;
}
