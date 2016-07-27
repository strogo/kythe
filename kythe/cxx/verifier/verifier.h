/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef KYTHE_CXX_VERIFIER_H_
#define KYTHE_CXX_VERIFIER_H_

#include <functional>
#include <string>

#include "kythe/proto/storage.pb.h"

#include "assertions.h"

namespace kythe {
namespace verifier {

/// \brief Runs logic programs.
///
/// The `Verifier` combines an `AssertionContext` with a database of Kythe
/// facts. It can be used to determine whether the goals specified in the
/// `AssertionContext` are satisfiable.
class Verifier {
 public:
  /// \param trace_lex Dump lexing debug information
  /// \param trace_parse Dump parsing debug information
  explicit Verifier(bool trace_lex = false, bool trace_parse = false);

  /// \brief Loads a source file with goal comments indicating rules and data.
  /// \param filename The filename to load
  /// \return false if we failed
  bool LoadInlineRuleFile(const std::string &filename);

  /// \brief Loads a text proto with goal comments indicating rules and data.
  /// \param file_data The data to load
  /// \return false if we failed
  bool LoadInlineProtoFile(const std::string &file_data);

  /// \brief During verification, ignore duplicate facts.
  void IgnoreDuplicateFacts();

  /// \brief Save results of verification keyed by inspection label.
  void SaveEVarAssignments();

  /// \brief Dump all goals to standard out.
  void ShowGoals();

  /// \brief Prints out a particular goal with its original source location
  /// to standard error.
  /// \param group_index The index of the goal's group.
  /// \param goal_index The index of the goal to print
  /// \sa highest_goal_reached, highest_group_reached
  void DumpErrorGoal(size_t group_index, size_t goal_index);

  /// \brief Dump known facts to standard out as a GraphViz graph.
  void DumpAsDot();

  /// \brief Dump known facts to standard out as JSON.
  void DumpAsJson();

  /// \brief Attempts to satisfy all goals from all loaded rule files and facts.
  /// \param inspect function to call on any inspection request
  /// \return true if all goals could be satisfied.
  bool VerifyAllGoals(std::function<bool(Verifier *context,
                                         const AssertionParser::Inspection &)>
                          inspect);

  /// \brief Attempts to satisfy all goals from all loaded rule files and facts.
  /// \return true if all goals could be satisfied.
  bool VerifyAllGoals();

  /// \brief Adds a single Kythe fact to the database.
  /// \param database_name some name used to define the database; should live
  /// as long as the `Verifier`. Used only for diagnostics.
  /// \param fact_id some identifier for the fact. Used only for diagnostics.
  void AssertSingleFact(std::string *database_name, unsigned int fact_id,
                        const kythe::proto::Entry &entry);

  /// \brief Perform basic well-formedness checks on the input database.
  /// \pre The database contains only fact-shaped terms, as generated by
  /// `AssertSingleFact`.
  /// \return false if the database was not well-formed.
  bool PrepareDatabase();

  /// Arena for allocating memory for both static data loaded from the database
  /// and dynamic data allocated during the course of evaluation.
  Arena *arena() { return &arena_; }

  /// Symbol table for uniquing strings.
  SymbolTable *symbol_table() { return &symbol_table_; }

  /// \brief Allocates an identifier for some token.
  /// \param location The source location for the identifier.
  /// \param token The text of the identifier.
  /// \return An `Identifier`. This may not be unique.
  Identifier *IdentifierFor(const yy::location &location,
                            const std::string &token);

  /// \brief Stringifies an integer, then makes an identifier out of it.
  /// \param location The source location for the identifier.
  /// \param integer The integer to stringify.
  /// \return An `Identifier`. This may not be unique.
  Identifier *IdentifierFor(const yy::location &location, int integer);

  /// \brief Convenience function to make `(App head (Tuple values))`.
  /// \param location The source location for the predicate.
  /// \param head The lhs of the `App` to allocate.
  /// \param values The body of the `Tuple` to allocate.
  AstNode *MakePredicate(const yy::location &location, AstNode *head,
                         std::initializer_list<AstNode *> values);

  /// \brief The head used for equality predicates.
  Identifier *eq_id() { return eq_id_; }

  /// \brief The head used for any VName predicate.
  AstNode *vname_id() { return vname_id_; }

  /// \brief The head used for any Fact predicate.
  AstNode *fact_id() { return fact_id_; }

  /// \brief The fact kind for an root/empty fact label.
  AstNode *root_id() { return root_id_; }

  /// \brief The empty string as an identifier.
  AstNode *empty_string_id() { return empty_string_id_; }

  /// \brief The fact kind for an edge ordinal.
  AstNode *ordinal_id() { return ordinal_id_; }

  /// \brief The fact kind used to assign a node its kind (eg /kythe/node/kind).
  AstNode *kind_id() { return kind_id_; }

  /// \brief Object for parsing and storing assertions.
  AssertionParser *parser() { return &parser_; }

  /// \brief Returns the highest group index the verifier reached during
  /// solving.
  size_t highest_group_reached() const { return highest_group_reached_; }

  /// \brief Returns the highest goal index the verifier reached during
  /// solving.
  size_t highest_goal_reached() const { return highest_goal_reached_; }

  /// \brief Change the prefix used to identify goals in source text.
  void set_goal_comment_marker(const std::string &it) {
    goal_comment_marker_ = it;
  }

 private:
  /// \brief Converts a VName proto to its AST representation.
  AstNode *ConvertVName(const yy::location &location,
                        const kythe::proto::VName &vname);

  /// \brief Adds an anchor VName.
  void AddAnchor(AstNode *vname, size_t begin, size_t end) {
    anchors_.emplace(std::make_pair(begin, end), vname);
  }

  /// \sa parser()
  AssertionParser parser_;

  /// \sa arena()
  Arena arena_;

  /// \sa symbol_table()
  SymbolTable symbol_table_;

  /// All known facts.
  std::vector<AstNode *> facts_;

  /// Multimap from anchor offsets to anchor VName tuples.
  std::multimap<std::pair<size_t, size_t>, AstNode *> anchors_;

  /// Has the database been prepared?
  bool database_prepared_ = false;

  /// Ignore duplicate facts during verification?
  bool ignore_dups_ = false;

  /// Filename to use for builtin constants.
  std::string builtin_location_name_;

  /// Location to use for builtin constants.
  yy::location builtin_location_;

  /// Node to use for the `=` identifier.
  Identifier *eq_id_;

  /// Node to use for the `vname` constant.
  AstNode *vname_id_;

  /// Node to use for the `fact` constant.
  AstNode *fact_id_;

  /// Node to use for the `/` constant.
  AstNode *root_id_;

  /// Node to use for the empty string constant.
  AstNode *empty_string_id_;

  /// Node to use for the `/kythe/ordinal` constant.
  AstNode *ordinal_id_;

  /// Node to use for the `/kythe/node/kind` constant.
  AstNode *kind_id_;

  /// Node to use for the `anchor` constant.
  AstNode *anchor_id_;

  /// Node to use for the `/kythe/loc/start` constant.
  AstNode *start_id_;

  /// Node to use for the `/kythe/loc/end` constant.
  AstNode *end_id_;

  /// The highest goal group reached during solving (often the culprit for why
  /// the solution failed).
  size_t highest_group_reached_ = 0;

  /// The highest goal reached during solving (often the culprit for why
  /// the solution failed).
  size_t highest_goal_reached_ = 0;

  /// Whether we save assignments to EVars (by inspection label).
  bool saving_assignments_ = false;

  /// A map from inspection label to saved assignment. Note that
  /// duplicate labels will overwrite one another. This means that
  /// it's important to disambiguate cases where this is likely
  /// (e.g., we add line and column information to labels we generate
  /// for anchors).
  std::map<std::string, AstNode *> saved_assignments_;

  /// The string to look for at the beginning of a goal comment.
  std::string goal_comment_marker_ = "//-";
};

}  // namespace verifier
}  // namespace kythe

#endif  // KYTHE_CXX_VERIFIER_H_
