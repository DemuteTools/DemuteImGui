// SPDX-License-Identifier: MIT
//
// The four kinds of Sound Library node, as far as the icons need to know. RAV declares this same
// enum in its sound_model.h; when RAV moves onto this library (story 1.3) it drops its own copy and
// includes this header (same name, same namespace: nothing else changes).

#pragma once

namespace rav {

enum class SoundNodeType { Sound, Random, Blend, Switch };

}  // namespace rav
