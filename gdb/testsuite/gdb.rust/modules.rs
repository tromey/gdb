// Copyright (C) 2016 Free Software Foundation, Inc.

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#![allow(dead_code)]
#![allow(unused_variables)]
#![allow(unused_assignments)]

fn f2() {
    println!("::f2");
}

pub mod mod1 {
    pub mod inner {
        pub mod innest {
            pub fn f1 () {
                let f2 = || println!("lambda f2");

                f2();           // set breakpoint here
                f3();
                self::f2();
                super::f2();
                self::super::f2();
                self::super::super::f2();
                super::super::f2();
                ::f2();
            }

            pub fn f2() {
                println!("mod1::inner::innest::f2");
            }

            pub fn f3() {
                println!("mod1::inner::innest::f3");
            }
        }

        pub fn f2() {
            println!("mod1::inner::f2");
        }
    }

    pub fn f2() {
        println!("mod1::f2");
    }
}

fn main () {
    mod1::inner::innest::f1();
}
