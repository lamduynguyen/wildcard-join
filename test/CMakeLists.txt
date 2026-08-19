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
endif()
