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


pub struct HiBob {
    pub field1: i32,
    field2: u64,
}

struct ByeBob(i32, u64);

enum Something {
    One,
    Two,
    Three
}

enum MoreComplicated {
    One,
    Two(i32),
    Three(HiBob)
}

fn diff2(x: i32, y: i32) -> i32 {
    x - y
}

pub struct Unit;

fn main () {
    let a = ();
    let b : [i32; 0] = [];

    let mut c = 27;
    let d = c = 99;

    let e = MoreComplicated::Two(73);

    let f = "hi bob";
    let g = b"hi bob";
    let h = b'9';

    let i = ["whatever"; 8];

    let j = Unit;

    let v = Something::Three;
    let w = [1,2,3,4];
    let x = (23, "hi");
    let y = HiBob {field1: 7, field2: 8};
    let z = ByeBob(7, 8);

    let slice = &w[2..3];
    let fromslice = slice[0];

    println!("{}, {}", x.0, x.1);        // set breakpoint here
    println!("{}", diff2(92, 45));
}
