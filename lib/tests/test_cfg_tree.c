/*
 * Copyright (c) 2018 Balabit
 * Copyright (c) 2013 Gergely Nagy <algernon@balabit.hu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include "cfg-tree.h"
#include "apphook.h"
#include "logpipe.h"

/*
 * The Always Pipe. Always returns the same thing at init time.
 */

typedef struct
{
  LogPipe super;

  gboolean return_value;
  gboolean init_called;
  gboolean deinit_called;
} AlmightyAlwaysPipe;

/*
 * Helper functions
 */

static gboolean
almighty_always_pipe_init (LogPipe *s)
{
  AlmightyAlwaysPipe *self = (AlmightyAlwaysPipe *)s;

  self->init_called = TRUE;
  return self->return_value;
}

static gboolean
almighty_always_pipe_deinit (LogPipe *s)
{
  AlmightyAlwaysPipe *self = (AlmightyAlwaysPipe *)s;

  self->deinit_called = TRUE;
  return TRUE;
}

static LogPipe *
almighty_always_pipe_new (gboolean return_value)
{
  AlmightyAlwaysPipe *self;

  self = g_new0 (AlmightyAlwaysPipe, 1);

  log_pipe_init_instance (&self->super, NULL);

  self->super.init = almighty_always_pipe_init;
  self->super.deinit = almighty_always_pipe_deinit;

  self->return_value = return_value;

  return &self->super;
}

static AlmightyAlwaysPipe *
create_and_attach_almighty_pipe (CfgTree *tree, gboolean pipe_value)
{
  AlmightyAlwaysPipe *pipe;

  pipe = (AlmightyAlwaysPipe *)almighty_always_pipe_new (pipe_value);
  cfg_tree_add_object (tree, log_expr_node_new_pipe (&pipe->super, NULL));

  return pipe;
}

/*
 * Tests
 */
typedef struct _PipeParameter
{
  gboolean always_pipe_value;
  gboolean tree_start_expected;
  gboolean tree_stop_expected;
  gboolean was_init_called;
  gboolean was_deinit_called;
} PipeParameter;

ParameterizedTestParameters(cfg_tree, test_pipe_init)
{
  static PipeParameter test_data_list[] =
  {
    {.always_pipe_value = TRUE, .tree_start_expected = TRUE, .tree_stop_expected = TRUE, .was_init_called = TRUE, .was_deinit_called = TRUE},
    {.always_pipe_value = FALSE, .tree_start_expected = FALSE, .tree_stop_expected = TRUE, .was_init_called = TRUE, .was_deinit_called = FALSE}
  };

  return cr_make_param_array(PipeParameter, test_data_list, sizeof(test_data_list) / sizeof(test_data_list[0]));
}

ParameterizedTest(PipeParameter *test_data, cfg_tree, test_pipe_init)
{
  CfgTree tree;
  AlmightyAlwaysPipe *pipe;

  cfg_tree_init_instance(&tree, NULL);
  tree.check_zero_source_count = FALSE;

  pipe = create_and_attach_almighty_pipe(&tree, test_data->always_pipe_value);

  cr_assert_eq(cfg_tree_start(&tree), test_data->tree_start_expected,
               "cfg_tree_start() did not return the expected value");
  cr_assert_eq(cfg_tree_stop(&tree), test_data->tree_stop_expected,
               "cfg_tree_stop() did not return the epxected value");

  cr_assert_eq(pipe->init_called, test_data->was_init_called,
               "->init was called state");
  cr_assert_eq(pipe->deinit_called, test_data->was_deinit_called,
               "->deinit was called state");

  cfg_tree_free_instance(&tree);
}

Test(cfg_tree, test_pipe_init_multi_success)
{
  CfgTree tree;

  cfg_tree_init_instance (&tree, NULL);
  tree.check_zero_source_count = FALSE;

  create_and_attach_almighty_pipe (&tree, TRUE);
  create_and_attach_almighty_pipe (&tree, TRUE);
  create_and_attach_almighty_pipe (&tree, TRUE);

  cr_assert(cfg_tree_start (&tree),
            "Starting a tree of all-good nodes works");
  cr_assert(cfg_tree_stop (&tree),
            "Stopping a tree of all-good nodes works");

  cfg_tree_free_instance (&tree);
}

Test(cfg_tree, test_pipe_init_multi_with_bad_node)
{
  AlmightyAlwaysPipe *pipe1, *pipe2, *pipe3;
  CfgTree tree;

  cfg_tree_init_instance (&tree, NULL);
  tree.check_zero_source_count = FALSE;

  pipe1 = create_and_attach_almighty_pipe (&tree, TRUE);
  pipe2 = create_and_attach_almighty_pipe (&tree, FALSE);
  pipe3 = create_and_attach_almighty_pipe (&tree, TRUE);

  cr_assert_not(cfg_tree_start (&tree),
                "Starting a tree of all-good nodes works");
  cr_assert(cfg_tree_stop (&tree),
            "Stopping a tree of all-good nodes works");

  cr_assert(pipe1->init_called,
            "The initializer of the first good pipe is called");
  cr_assert(pipe2->init_called,
            "The initializer of the bad pipe is called");
  cr_assert_not(pipe3->init_called,
                "The initializer of the second good pipe is NOT called");

  cr_assert(pipe1->deinit_called,
            "The deinitializer of the first good pipe is called");
  cr_assert_not(pipe2->deinit_called,
                "The deinitializer of the bad pipe is NOT called");
  cr_assert_not(pipe3->deinit_called,
                "The deinitializer of the second good pipe is NOT called");

  cfg_tree_free_instance (&tree);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(cfg_tree, .init = setup, .fini = teardown);
