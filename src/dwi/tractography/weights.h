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

#ifndef __dwi_tractography_weights_h__
#define __dwi_tractography_weights_h__

#include "cmdline_option.h"

namespace MR
{
  namespace DWI
  {

    namespace Tractography
    {

      extern const App::Option TrackWeightsInOption;
      extern const App::Option TrackWeightsOutOption;

    }
  }
}

#endif

