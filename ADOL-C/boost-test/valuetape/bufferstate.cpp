#define BOOST_TEST_DYN_LINK
#include <adolc/adolc.h>
#include <boost/test/unit_test.hpp>
#include <cstddef>

BOOST_AUTO_TEST_SUITE(Test_BufferState)

static_assert(
    ADOLC::detail::BufferStateType<ADOLC::detail::OpBuffer, unsigned char>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::LocBuffer, size_t>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::ValBuffer, double>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::TayBuffer, double>);

BOOST_AUTO_TEST_CASE(TestViewLazilyAllocatesIndependentStorage) {
  ADOLC::detail::ValBuffer owner(new double[3]{1.0, 2.0, 3.0}, 3);
  ADOLC::detail::ValBuffer view(owner, ADOLC::detail::bufferView);

  BOOST_REQUIRE(!view.isOwner());
  BOOST_CHECK_EQUAL(view.begin(), owner.begin());
  BOOST_CHECK_EQUAL(view[1], 2.0);

  view.allocateAndOwn();

  BOOST_REQUIRE(view.isOwner());
  BOOST_CHECK_NE(view.begin(), owner.begin());
  view[0] = 9.0;
  BOOST_CHECK_EQUAL(owner[0], 1.0);
  BOOST_REQUIRE(owner.isOwner());
}

BOOST_AUTO_TEST_CASE(TestCopyIsDeepAndPreservesState) {
  ADOLC::detail::ValBuffer source(new double[3]{1.0, 2.0, 3.0}, 3);
  source.position(2);
  source.numOnTape(7);

  ADOLC::detail::ValBuffer copy(source);

  BOOST_REQUIRE(copy.isOwner());
  BOOST_REQUIRE(source.isOwner());
  BOOST_CHECK_NE(copy.begin(), source.begin());
  BOOST_CHECK_EQUAL(copy[0], 1.0);
  BOOST_CHECK_EQUAL(copy[1], 2.0);
  BOOST_CHECK_EQUAL(copy[2], 3.0);
  BOOST_CHECK_EQUAL(copy.position(), size_t{2});
  BOOST_CHECK_EQUAL(copy.capacity(), size_t{3});
  BOOST_CHECK_EQUAL(copy.numOnTape(), size_t{7});
}

BOOST_AUTO_TEST_CASE(TestAssigningEmptyClearsOwnedAndViewedStorage) {
  ADOLC::detail::ValBuffer empty;
  empty.numOnTape(4);

  ADOLC::detail::ValBuffer owned(new double[2]{1.0, 2.0}, 2);
  owned = empty;
  BOOST_CHECK(owned.isOwner());
  BOOST_CHECK_EQUAL(owned.begin(), nullptr);
  BOOST_CHECK_EQUAL(owned.capacity(), size_t{0});
  BOOST_CHECK_EQUAL(owned.numOnTape(), size_t{4});

  ADOLC::detail::ValBuffer source(new double[2]{3.0, 4.0}, 2);
  ADOLC::detail::ValBuffer view(source, ADOLC::detail::bufferView);
  view = empty;
  BOOST_CHECK(view.isOwner());
  BOOST_CHECK_EQUAL(view.begin(), nullptr);
  BOOST_CHECK_EQUAL(view.capacity(), size_t{0});
  BOOST_CHECK_EQUAL(view.numOnTape(), size_t{4});
  BOOST_CHECK_EQUAL(source[0], 3.0);
}

BOOST_AUTO_TEST_SUITE_END()
