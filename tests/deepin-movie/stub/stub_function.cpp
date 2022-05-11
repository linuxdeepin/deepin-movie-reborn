/*
 * MIT License
 *
 * Copyright (c) 2019 coolxv
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "stub.h"
#include "stub_function.h"

namespace StubFunc{
bool isPadSystemTrue_stub()
{
    return true;
}
bool isCompositedTrue_stub()
{
    return true;
}
bool isCompositedFalse_stub()
{
    return false;
}

PlayerEngine::CoreState playerEngineState_Paused_stub(void* obj)
{
    PlayerEngine * engine = (PlayerEngine *)obj;
    return PlayerEngine::CoreState::Paused;
}

void createSelectableLineEditOptionHandle_lambda_stub(void *obj)
{
    qDebug() << "shortcut save path btn clicked.";
}

bool isMpvExists_stub()
{
    return false;
}

}
