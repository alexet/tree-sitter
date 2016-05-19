#include "spec_helper.h"
#include "compiler/build_tables/lex_item.h"
#include "compiler/rules/metadata.h"
#include "compiler/rules.h"
#include "helpers/rule_helpers.h"
#include "helpers/stream_methods.h"

using namespace rules;
using namespace build_tables;
typedef LexItemSet::Transition Transition;

START_TEST

describe("LexItem", []() {
  describe("completion_status()", [&]() {
    it("indicates whether the item is done, its precedence, and whether it is a string", [&]() {
      LexItem item1(Symbol(0, true), character({ 'a', 'b', 'c' }));
      AssertThat(item1.completion_status().is_done, IsFalse());
      AssertThat(item1.completion_status().precedence, Equals(PrecedenceRange()));
      AssertThat(item1.completion_status().is_string, IsFalse());

      LexItem item2(Symbol(0, true), choice({
        metadata(blank(), { {PRECEDENCE, 3}, {IS_STRING, 1} }),
        character({ 'a', 'b', 'c' })
      }));

      AssertThat(item2.completion_status().is_done, IsTrue());
      AssertThat(item2.completion_status().precedence, Equals(PrecedenceRange(3)));
      AssertThat(item2.completion_status().is_string, IsTrue());

      LexItem item3(Symbol(0, true), repeat(character({ ' ', '\t' })));
      AssertThat(item3.completion_status().is_done, IsTrue());
      AssertThat(item3.completion_status().precedence, Equals(PrecedenceRange()));
      AssertThat(item3.completion_status().is_string, IsFalse());
    });
  });
});

describe("LexItemSet::transitions()", [&]() {
  it("handles single characters", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), character({ 'x' })),
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('x'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), blank()),
            }),
            PrecedenceRange(),
            false
          }
        }
      })));
  });

  it("marks transitions that are within the main token (as opposed to separators)", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), metadata(character({ 'x' }), {
        {MAIN_TOKEN, true}
      })),
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('x'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), metadata(blank(), { {MAIN_TOKEN, true}})),
            }),
            PrecedenceRange(),
            true
          }
        }
      })));
  });

  it("handles sequences", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), seq({
        character({ 'w' }),
        character({ 'x' }),
        character({ 'y' }),
        character({ 'z' }),
      })),
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('w'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), seq({
                character({ 'x' }),
                character({ 'y' }),
                character({ 'z' }),
              })),
            }),
            PrecedenceRange(),
            false
          }
        }
      })));
  });

  it("handles sequences with nested precedence", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), seq({
        prec(3, seq({
          character({ 'v' }),
          prec(4, seq({
            character({ 'w' }),
            character({ 'x' }) })),
          character({ 'y' }) })),
        character({ 'z' }),
      })),
    });

    auto transitions = item_set.transitions();

    AssertThat(
      transitions,
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('v'),
          Transition{
            // The outer precedence is now 'active', because we are within its
            // contained rule.
            LexItemSet({
              LexItem(Symbol(1), seq({
                active_prec(3, seq({
                  prec(4, seq({
                    character({ 'w' }),
                    character({ 'x' }) })),
                  character({ 'y' }) })),
                character({ 'z' }),
              })),
            }),

            // No precedence is applied upon entering a rule.
            PrecedenceRange(),
            false
          }
        }
      })));

    LexItemSet item_set2 = transitions[CharacterSet().include('v')].destination;
    transitions = item_set2.transitions();

    AssertThat(
      transitions,
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('w'),
          Transition{
            // The inner precedence is now 'active'
            LexItemSet({
              LexItem(Symbol(1), seq({
                active_prec(3, seq({
                  active_prec(4, character({ 'x' })),
                  character({ 'y' }) })),
                character({ 'z' }),
              })),
            }),

            // The outer precedence is applied.
            PrecedenceRange(3),
            false
          }
        }
      })));

    LexItemSet item_set3 = transitions[CharacterSet().include('w')].destination;
    transitions = item_set3.transitions();

    AssertThat(
      transitions,
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('x'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), seq({
                active_prec(3, character({ 'y' })),
                character({ 'z' }),
              })),
            }),

            // The inner precedence is applied.
            PrecedenceRange(4),
            false
          }
        }
      })));

    LexItemSet item_set4 = transitions[CharacterSet().include('x')].destination;
    transitions = item_set4.transitions();

    AssertThat(
      transitions,
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('y'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), character({ 'z' })),
            }),
            PrecedenceRange(3),
            false
          }
        }
      })));
  });

  it("handles sequences where the left hand side can be blank", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), seq({
        choice({
          character({ 'x' }),
          blank(),
        }),
        character({ 'y' }),
        character({ 'z' }),
      })),
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('x'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), seq({
                character({ 'y' }),
                character({ 'z' }),
              })),
            }),
            PrecedenceRange(),
            false
          }
        },
        {
          CharacterSet().include('y'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), character({ 'z' })),
            }),
            PrecedenceRange(),
            false
          }
        }
      })));
  });

  it("handles blanks", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), blank()),
    });

    AssertThat(item_set.transitions(), IsEmpty());
  });

  it("handles repeats", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), repeat1(seq({
        character({ 'a' }),
        character({ 'b' }),
      }))),
      LexItem(Symbol(2), repeat1(character({ 'c' }))),
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('a'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), seq({
                character({ 'b' }),
                repeat1(seq({
                  character({ 'a' }),
                  character({ 'b' }),
                }))
              })),
              LexItem(Symbol(1), character({ 'b' })),
            }),
            PrecedenceRange(),
            false
          }
        },
        {
          CharacterSet().include('c'),
          Transition{
            LexItemSet({
              LexItem(Symbol(2), repeat1(character({ 'c' }))),
              LexItem(Symbol(2), blank()),
            }),
            PrecedenceRange(),
            false
          }
        }
      })));
  });

  it("handles repeats with precedence", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), active_prec(-1, repeat1(character({ 'a' }))))
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('a'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), active_prec(-1, repeat1(character({ 'a' })))),
              LexItem(Symbol(1), active_prec(-1, blank())),
            }),
            PrecedenceRange(-1),
            false
          }
        }
      })));
  });

  it("handles choices between overlapping character sets", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), choice({
        active_prec(2, seq({
          character({ 'a', 'b', 'c', 'd'  }),
          character({ 'x' }),
        })),
        active_prec(3, seq({
          character({ 'c', 'd', 'e', 'f' }),
          character({ 'y' }),
        })),
      }))
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('a', 'b'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), active_prec(2, character({ 'x' }))),
            }),
            PrecedenceRange(2),
            false
          }
        },
        {
          CharacterSet().include('c', 'd'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), active_prec(2, character({ 'x' }))),
              LexItem(Symbol(1), active_prec(3, character({ 'y' }))),
            }),
            PrecedenceRange(2, 3),
            false
          }
        },
        {
          CharacterSet().include('e', 'f'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), active_prec(3, character({ 'y' }))),
            }),
            PrecedenceRange(3),
            false
          }
        },
      })));
  });

  it("handles choices between a subset and a superset of characters", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), choice({
        seq({
          character({ 'b', 'c', 'd' }),
          character({ 'x' }),
        }),
        seq({
          character({ 'a', 'b', 'c', 'd', 'e', 'f' }),
          character({ 'y' }),
        }),
      })),
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include('a').include('e', 'f'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), character({ 'y' })),
            }),
            PrecedenceRange(),
            false
          }
        },
        {
          CharacterSet().include('b', 'd'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), character({ 'x' })),
              LexItem(Symbol(1), character({ 'y' })),
            }),
            PrecedenceRange(),
            false
          }
        },
      })));
  });

  it("handles choices between whitelisted and blacklisted character sets", [&]() {
    LexItemSet item_set({
      LexItem(Symbol(1), seq({
        choice({
          character({ '/' }, false),
          seq({
            character({ '\\' }),
            character({ '/' }),
          }),
        }),
        character({ '/' }),
      }))
    });

    AssertThat(
      item_set.transitions(),
      Equals(LexItemSet::TransitionMap({
        {
          CharacterSet().include_all().exclude('/').exclude('\\'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), character({ '/' })),
            }),
            PrecedenceRange(),
            false
          }
        },
        {
          CharacterSet().include('\\'),
          Transition{
            LexItemSet({
              LexItem(Symbol(1), character({ '/' })),
              LexItem(Symbol(1), seq({ character({ '/' }), character({ '/' }) })),
            }),
            PrecedenceRange(),
            false
          }
        },
      })));
  });

  it("handles different items with overlapping character sets", [&]() {
    LexItemSet set1({
      LexItem(Symbol(1), character({ 'a', 'b', 'c', 'd', 'e', 'f' })),
      LexItem(Symbol(2), character({ 'e', 'f', 'g', 'h', 'i' }))
    });

    AssertThat(set1.transitions(), Equals(LexItemSet::TransitionMap({
      {
        CharacterSet().include('a', 'd'),
        Transition{
          LexItemSet({
            LexItem(Symbol(1), blank()),
          }),
          PrecedenceRange(),
          false
        }
      },
      {
        CharacterSet().include('e', 'f'),
        Transition{
          LexItemSet({
            LexItem(Symbol(1), blank()),
            LexItem(Symbol(2), blank()),
          }),
          PrecedenceRange(),
          false
        }
      },
      {
        CharacterSet().include('g', 'i'),
        Transition{
          LexItemSet({
            LexItem(Symbol(2), blank()),
          }),
          PrecedenceRange(),
          false
        }
      },
    })));
  });
});

END_TEST
