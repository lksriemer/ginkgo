/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2020, the Ginkgo authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************<GINKGO LICENSE>*******************************/

#include <ginkgo/core/base/array.hpp>


#include <gtest/gtest.h>


#include <ginkgo/core/base/executor.hpp>


#include "core/test/utils.hpp"


namespace {


template <typename T>
class Array : public ::testing::Test {
protected:
    Array() : exec(gko::ReferenceExecutor::create()), x(exec, 2)
    {
        x.get_data()[0] = 5;
        x.get_data()[1] = 2;
    }

    static void assert_equal_to_original_x(gko::Array<T> &a)
    {
        ASSERT_EQ(a.get_num_elems(), 2);
        EXPECT_EQ(a.get_data()[0], T{5});
        EXPECT_EQ(a.get_data()[1], T{2});
        EXPECT_EQ(a.get_const_data()[0], T{5});
        EXPECT_EQ(a.get_const_data()[1], T{2});
    }

    std::shared_ptr<const gko::Executor> exec;
    gko::Array<T> x;
};

TYPED_TEST_CASE(Array, gko::test::ValueAndIndexTypes);


TYPED_TEST(Array, CanBeCreatedWithoutAnExecutor)
{
    gko::Array<TypeParam> a;

    ASSERT_EQ(a.get_executor(), nullptr);
    ASSERT_EQ(a.get_num_elems(), 0);
}


TYPED_TEST(Array, CanBeEmpty)
{
    gko::Array<TypeParam> a(this->exec);

    ASSERT_EQ(a.get_num_elems(), 0);
}


TYPED_TEST(Array, ReturnsNullWhenEmpty)
{
    gko::Array<TypeParam> a(this->exec);

    EXPECT_EQ(a.get_const_data(), nullptr);
    ASSERT_EQ(a.get_data(), nullptr);
}


TYPED_TEST(Array, CanBeCreatedFromExistingData)
{
    gko::Array<TypeParam> a{this->exec, 3, new TypeParam[3],
                            std::default_delete<TypeParam[]>{}};

    EXPECT_EQ(a.get_num_elems(), 3);
}


TYPED_TEST(Array, CanBeCreatedFromDataOnExecutor)
{
    gko::Array<TypeParam> a{
        this->exec, 3,
        this->exec->get_mem_space()->template alloc<TypeParam>(3)};

    EXPECT_EQ(a.get_num_elems(), 3);
}


TYPED_TEST(Array, CanBeCreatedFromRange)
{
    using std::begin;
    auto data = {1, 2, 3};

    gko::Array<TypeParam> a{this->exec, begin(data), end(data)};

    EXPECT_EQ(a.get_const_data()[0], TypeParam{1});
    EXPECT_EQ(a.get_const_data()[1], TypeParam{2});
    ASSERT_EQ(a.get_const_data()[2], TypeParam{3});
}


TYPED_TEST(Array, CanBeCreatedFromInitializerList)
{
    gko::Array<TypeParam> a{this->exec, {1, 2, 3}};

    EXPECT_EQ(a.get_const_data()[0], TypeParam{1});
    EXPECT_EQ(a.get_const_data()[1], TypeParam{2});
    ASSERT_EQ(a.get_const_data()[2], TypeParam{3});
}


TYPED_TEST(Array, KnowsItsSize) { ASSERT_EQ(this->x.get_num_elems(), 2); }


TYPED_TEST(Array, ReturnsValidDataPtr)
{
    EXPECT_EQ(this->x.get_data()[0], TypeParam{5});
    EXPECT_EQ(this->x.get_data()[1], TypeParam{2});
}


TYPED_TEST(Array, ReturnsValidConstDataPtr)
{
    EXPECT_EQ(this->x.get_const_data()[0], TypeParam{5});
    EXPECT_EQ(this->x.get_const_data()[1], TypeParam{2});
}


TYPED_TEST(Array, KnowsItsExecutor)
{
    ASSERT_EQ(this->x.get_executor(), this->exec);
}


TYPED_TEST(Array, CanBeCopyConstructed)
{
    gko::Array<TypeParam> a(this->x);
    this->x.get_data()[0] = 7;

    this->assert_equal_to_original_x(a);
}


TYPED_TEST(Array, CanBeMoveConstructed)
{
    gko::Array<TypeParam> a(std::move(this->x));

    this->assert_equal_to_original_x(a);
}


TYPED_TEST(Array, CanBeCopyConstructedToADifferentExecutor)
{
    gko::Array<TypeParam> a{this->exec, this->x};

    this->assert_equal_to_original_x(a);
}


TYPED_TEST(Array, CanBeMoveConstructedToADifferentExecutor)
{
    gko::Array<TypeParam> a{this->exec, std::move(this->x)};

    this->assert_equal_to_original_x(a);
}


TYPED_TEST(Array, CanBeCopied)
{
    auto omp = gko::OmpExecutor::create();
    gko::Array<TypeParam> a(omp, 3);

    a = this->x;
    this->x.get_data()[0] = 7;

    this->assert_equal_to_original_x(a);
}


TYPED_TEST(Array, CanBeCopiedToExecutorlessArray)
{
    gko::Array<TypeParam> a;

    a = this->x;

    ASSERT_EQ(a.get_executor(), this->x.get_executor());
    this->assert_equal_to_original_x(a);
}


TYPED_TEST(Array, CanBeCopiedFromExecutorlessArray)
{
    gko::Array<TypeParam> a;

    this->x = a;

    ASSERT_NE(this->x.get_executor(), nullptr);
    ASSERT_EQ(this->x.get_num_elems(), 0);
}


TYPED_TEST(Array, CanBeMoved)
{
    auto omp = gko::OmpExecutor::create();
    gko::Array<TypeParam> a(omp, 3);

    a = std::move(this->x);

    this->assert_equal_to_original_x(a);
}


TYPED_TEST(Array, CanBeMovedToExecutorlessArray)
{
    gko::Array<TypeParam> a;

    a = std::move(this->x);

    ASSERT_NE(a.get_executor(), nullptr);
    this->assert_equal_to_original_x(a);
}


TYPED_TEST(Array, CanBeMovedFromExecutorlessArray)
{
    gko::Array<TypeParam> a;

    this->x = std::move(a);

    ASSERT_NE(this->x.get_executor(), nullptr);
    ASSERT_EQ(this->x.get_num_elems(), 0);
}


TYPED_TEST(Array, CanBeCleared)
{
    this->x.clear();

    ASSERT_EQ(this->x.get_num_elems(), 0);
    ASSERT_EQ(this->x.get_data(), nullptr);
    ASSERT_EQ(this->x.get_const_data(), nullptr);
}


TYPED_TEST(Array, CanBeResized)
{
    this->x.resize_and_reset(3);

    this->x.get_data()[0] = 1;
    this->x.get_data()[1] = 8;
    this->x.get_data()[2] = 7;

    EXPECT_EQ(this->x.get_const_data()[0], TypeParam{1});
    EXPECT_EQ(this->x.get_const_data()[1], TypeParam{8});
    EXPECT_EQ(this->x.get_const_data()[2], TypeParam{7});
}


TYPED_TEST(Array, CanBeAssignedAnExecutor)
{
    gko::Array<TypeParam> a;

    a.set_executor(this->exec);

    ASSERT_EQ(a.get_executor(), this->exec);
}


TYPED_TEST(Array, ChangesExecutors)
{
    auto omp = gko::OmpExecutor::create();
    this->x.set_executor(omp);

    ASSERT_EQ(this->x.get_executor(), omp);
    this->assert_equal_to_original_x(this->x);
}


TYPED_TEST(Array, CanCreateView)
{
    TypeParam data[] = {1, 2, 3};

    auto view = gko::Array<TypeParam>::view(this->exec, 3, data);
    view = gko::Array<TypeParam>{this->exec, {5, 4, 2}};

    EXPECT_EQ(data[0], TypeParam{5});
    EXPECT_EQ(data[1], TypeParam{4});
    EXPECT_EQ(data[2], TypeParam{2});
}


}  // namespace
