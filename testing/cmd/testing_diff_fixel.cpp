/*
 * Copyright (c) 2008-2016 the MRtrix3 contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/
 *
 * MRtrix is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * For more details, see www.mrtrix.org
 *
 */

#include "command.h"
#include "datatype.h"


#include "image.h"
#include "fixel_format/helpers.h"

#include "diff_images.h"

using namespace MR;
using namespace App;

void usage ()
{
  AUTHOR = "David Raffelt (david.raffelt@florey.edu.au) and Robert E. Smith (robert.smith@florey.edu.au)";

  DESCRIPTION
  + "compare two fixel images for differences, within specified tolerance.";

  ARGUMENTS
  + Argument ("fixel1", "fixel folder.").type_text ()
  + Argument ("fixel2", "another fixel folder.").type_text ();

  OPTIONS
  + Testing::Diff_Image_Options;

}


void run ()
{
  std::string fixel_folder1 = argument[0];
  FixelFormat::check_fixel_folder (fixel_folder1);
  std::string fixel_folder2 = argument[1];
  FixelFormat::check_fixel_folder (fixel_folder2);

  if (fixel_folder1 == fixel_folder2)
    throw Exception ("Input fixel folders are the same");

  auto dir_walker1 = Path::Dir (fixel_folder1);
  std::string fname;
  while ((fname = dir_walker1.read_name()).size()) {
    auto in1 = Image<cdouble>::open (Path::join (fixel_folder1, fname));
    std::string filename2 = Path::join (fixel_folder2, fname);
    if (!Path::exists (filename2))
      throw Exception ("File (" + fname + ") exists in fixel folder (" + fixel_folder1 + ") but not in fixel folder (" + fixel_folder2 + ") ");
    auto in2 = Image<cdouble>::open (filename2);
    Testing::diff_images (in1, in2);
  }
  auto dir_walker2 = Path::Dir (fixel_folder2);
  while ((fname = dir_walker2.read_name()).size()) {
    std::string filename1 = Path::join (fixel_folder1, fname);
    if (!Path::exists (filename1))
      throw Exception ("File (" + fname + ") exists in fixel folder (" + fixel_folder2 + ") but not in fixel folder (" + fixel_folder1 + ") ");
  }
  CONSOLE ("data checked OK");
}

