if(ENABLE_TESTING)
  include(GoogleTest)

  # Both test binaries define their own main() and call InitGoogleTest, so
  # GTest::gtest_main is not linked.  GTest::gmock is not linked either: no
  # test uses a mock, and Ubuntu ships gmock in a separate package, so asking
  # for it fails configure on a stock libgtest-dev install.
  set(TEST_REQUIRED_LIBS ahocorasick GTest::gtest)

  # Not named ADD_TEST: cmake command names are case insensitive, so that
  # would shadow the built-in add_test().
  function(ac_add_test TEST_NAME)
    add_executable(${ARGV})
    target_link_libraries(${TEST_NAME} PRIVATE ${TEST_REQUIRED_LIBS})
    gtest_discover_tests(${TEST_NAME})
  endfunction()

  ac_add_test(TestAhoCorasick test_ahocorasick.cc)
  ac_add_test(TestMatching test_matching.cc)
  ac_add_test(TestReuse test_reuse.cc)
  # Not an ac_add_test: this one has no gtest in it, so gtest_discover_tests
  # would run it once with --gtest_list_tests, get nothing, and register
  # nothing. It is a plain binary with a wall clock budget.
  #
  # ctest gets a short run so that a normal `ctest` is still seconds rather
  # than a minute. CI runs the same binary with --seconds 60 as its own step,
  # which is where the depth comes from. The seed is fixed either way, so a
  # failure here is a failure everyone gets and not a lottery.
  add_executable(FuzzDifferential fuzz_differential.cc)
  target_link_libraries(FuzzDifferential PRIVATE ahocorasick)
  add_test(NAME FuzzDifferential.Short COMMAND FuzzDifferential --seconds 2.5)
endif()
