/*****************************************************************************
*  ITU-R P.525 Free Space Path Loss model for Signal Server by Alex Farrant  *
*  15 January 2014                                                           *
*  optimised G6DTX April 2017                                                *
*  This program is free software; you can redistribute it and/or modify it   *
*  under the terms of the GNU General Public License as published by the     *
*  Free Software Foundation; either version 2 of the License or any later    *
*  version.                                                                  *
*                                                                            *
*  This program is distributed in the hope that it will useful, but WITHOUT  *
*  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or     *
*  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License     *
*  for more details.                                                         *
*
* https://www.itu.int/rec/R-REC-P.525/en
* Free Space Path Loss model
* Frequency: Any
* Distance: Any
*/

#include <math.h>

// use call with log/ln as this may be faster
// use constant of value 20.0/log(10.0)
static __inline float _20log10f(float x)
{
  return(8.685889f*logf(x));
}

double FSPLpathLoss(float f, float d)
{
  return(32.44 + _20log10f(f) + _20log10f(d));
}
