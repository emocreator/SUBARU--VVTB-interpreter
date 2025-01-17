#include "../include/common.h"
#include "../include/subaruu.h"
#include "../include/tokenizer.h"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string_view>

/**
 * Constructs a new SUBARUU object and initialize with the given source file.
 *
 * @param source The source code file.
 * @throws std::runtime_error if tokenizer initialization fails
 */
SUBARUU::SUBARUU(std::string_view source)
  : tokenizer_(std::make_unique<Tokenizer>(source))
  , execution_finished_(false)
  , skip_to_line_(false)
  , target_line_(-1) {

    if (!tokenizer_) {
        throw std::runtime_error("Failed to initialize Tokenizer");
    }

    // Initialize all variables (a-z) to 0
    for (char c = 'a'; c <= 'z'; ++c) {
        variables_[c] = 0;
    }
}

/**
 * Runs the SUBARUU interpreter.
 */
void SUBARUU::run() {
    DEBUG_LOG("Starting program execution");

    // Build line map at start
    build_line_map();

    while (!finished()) {
        if (tokenizer_->finished()) {
            execution_finished_ = true;
            break;
        }

        if (skip_to_line_) {
            // We're looking for a specific line
            auto token = tokenizer_->current_token();
            if (token == Tokenizer::TokenType::NUMBER) {
                int current_line = tokenizer_->get_num();
                if (current_line == target_line_) {
                    skip_to_line_ = false;
                    target_line_ = -1;
                    tokenizer_->next_token(); // Move past the line number
                    statement();
                } else {
                    // Skip to next line
                    while (tokenizer_->current_token() !=
                             Tokenizer::TokenType::EOL &&
                           !tokenizer_->finished()) {
                        tokenizer_->next_token();
                    }
                    if (tokenizer_->current_token() ==
                        Tokenizer::TokenType::EOL) {
                        tokenizer_->next_token();
                    }
                }
            } else {
                // Skip non-numbered lines while searching
                while (tokenizer_->current_token() !=
                         Tokenizer::TokenType::EOL &&
                       !tokenizer_->finished()) {
                    tokenizer_->next_token();
                }
                if (tokenizer_->current_token() == Tokenizer::TokenType::EOL) {
                    tokenizer_->next_token();
                }
            }
        } else {
            line_statement();
        }
    }
    DEBUG_LOG("Program execution finished");
}

/**
 * Gets the string representation of a token.
 *
 * @param token The token type to convert to string
 * @return std::string The string representation of the token
 */
std::string SUBARUU::get_token_string(Tokenizer::TokenType token) const {
    return std::string(tokenizer_->token_to_string(token));
}

/**
 * Checks if the interpreter has finished execution.
 *
 * @return true if execution has finished
 * @return false if execution is still ongoing
 */
bool SUBARUU::finished() const { return execution_finished_; }

/**
 * Debug print function with error handling.
 * Prints message to stderr and throws for errors but not warnings.
 *
 * @param message The message to print
 * @param errorCode E_ERROR or E_WARNING
 * @throws std::runtime_error if errorCode is E_ERROR
 */
void SUBARUU::dprintf(const std::string& message, int errorCode) {
    if (errorCode == E_ERROR) {
        std::cerr << "ERROR: " << message << std::endl;
        throw std::runtime_error(message);
    } else {
        std::cerr << "WARNING: " << message << std::endl;
    }
}

/**
 * Accepts the expected token or reports an error.
 *
 * @param expectedToken The token type that should be next in the stream
 * @throws std::runtime_error if the current token doesn't match expected
 */
void SUBARUU::accept(Tokenizer::TokenType expectedToken) {
    if (tokenizer_->current_token() != expectedToken) {
        std::string error = "*subaruu.cpp: unexpected `" +
                            get_token_string(tokenizer_->current_token()) +
                            "` expected `" + get_token_string(expectedToken) +
                            "`";
        dprintf(error, E_ERROR);
    }
    tokenizer_->next_token();
}

/**
 * Parses and evaluates a factor in the expression.
 * A factor can be:
 * - A number
 * - A variable
 * - A parenthesized expression
 *
 * @return int The evaluated value of the factor
 * @throws std::runtime_error on syntax errors
 */
int SUBARUU::factor() {
    int result = 0;
    const auto token = tokenizer_->current_token();

    switch (token) {
        case Tokenizer::TokenType::NUMBER:
            result = tokenizer_->get_num();
            DEBUG_LOG("Factor number: " << result);
            tokenizer_->next_token();
            break;

        case Tokenizer::TokenType::LETTER: {
            char var_name =
              std::tolower(std::get<char>(tokenizer_->get_token_data()));
            DEBUG_LOG("Factor variable " << var_name);

            auto it = variables_.find(var_name);
            result = (it != variables_.end()) ? it->second : 0;

            DEBUG_LOG(" = " << result);
            tokenizer_->next_token();
            break;
        }

        case Tokenizer::TokenType::LEFT_PAREN:
            tokenizer_->next_token(); // Consume '('
            result = expression();
            accept(Tokenizer::TokenType::RIGHT_PAREN);
            break;

        default:
            dprintf("Syntax Error: Unexpected token in factor: " +
                      std::string(tokenizer_->token_to_string(token)),
                    E_ERROR);
    }

    return result;
}

/**
 * Parses and evaluates a term in the expression.
 * A term consists of factors connected by * or / operators.
 *
 * @return int The evaluated term value
 */
int SUBARUU::term() {
    DEBUG_LOG("Starting term evaluation");

    int result = factor();
    auto token = tokenizer_->current_token();
    DEBUG_LOG("Term token: " << get_token_string(token));

    while (token == Tokenizer::TokenType::ASTERISK ||
           token == Tokenizer::TokenType::SLASH) {
        tokenizer_->next_token();
        int factor_value = factor();

        if (token == Tokenizer::TokenType::ASTERISK) {
            result *= factor_value;
        } else { // SLASH
            result = safe_divide(result, factor_value);
        }

        token = tokenizer_->current_token();
    }

    return result;
}

/**
 * Parses and evaluates a complete expression.
 * An expression consists of terms connected by + or - operators.
 * Also handles special case of line numbers.
 *
 * @return int The evaluated expression value
 */
int SUBARUU::expression() {
    DEBUG_LOG("Starting expression evaluation");

    int result = term();
    auto token = tokenizer_->current_token();
    DEBUG_LOG("Expression token after term: " << get_token_string(token));

    // Check for potential line number
    if (token == Tokenizer::TokenType::NUMBER) {
        if (is_valid_line_number(tokenizer_->get_num())) {
            return result;
        }
    }

    while (token == Tokenizer::TokenType::PLUS ||
           token == Tokenizer::TokenType::MINUS) {
        tokenizer_->next_token();
        int term_value = term();

        if (token == Tokenizer::TokenType::PLUS) {
            result += term_value;
        } else { // MINUS
            result -= term_value;
        }

        token = tokenizer_->current_token();
    }

    return result;
}

/**
 * Helper to determine if a number could be a valid line number.
 * Line numbers must be >= 10 and multiples of 10, followed by whitespace.
 *
 * @param num The number to check
 * @return bool True if this appears to be a line number
 */
bool SUBARUU::is_valid_line_number(int num) const {
    if (num >= 10 && num % 10 == 0) {
        if (tokenizer_->finished()) {
            return true; // EOF after line number is acceptable
        }
        char next_char = tokenizer_->peek_char();
        return (next_char == ' ' || next_char == '\n' || next_char == '\r');
    }
    return false;
}

/**
 * Performs safe division with error handling.
 *
 * @param numerator The division numerator
 * @param denominator The division denominator
 * @return int The division result or 0 for division by zero
 */
int SUBARUU::safe_divide(int numerator, int denominator) {
    if (denominator == 0) {
        dprintf("*warning: divide by zero", E_WARNING);
        DEBUG_LOG("Division by zero detected, setting result to 0");
        return 0;
    }

    int result = numerator / denominator;
    DEBUG_LOG("Division result: " << result);
    return result;
}

/**
 * Parses and evaluates a relation (comparison expression).
 * Returns 1 for true conditions, 0 for false.
 * Non-zero values in simple expressions are treated as true.
 *
 * @return int 1 for true, 0 for false
 * @throws std::runtime_error on invalid comparison operator
 */
int SUBARUU::relation() {
    DEBUG_LOG("Starting relation evaluation");

    int left = expression();
    DEBUG_LOG("Left side of relation: " << left);

    auto token = tokenizer_->current_token();
    DEBUG_LOG("Relation operator: " << get_token_string(token));

    // Check if this is a comparison operation
    switch (token) {
        case Tokenizer::TokenType::EQUAL:
        case Tokenizer::TokenType::LT:
        case Tokenizer::TokenType::GT:
        case Tokenizer::TokenType::LT_EQ:
        case Tokenizer::TokenType::GT_EQ:
        case Tokenizer::TokenType::NOT_EQUAL: {
            auto op = token;
            tokenizer_->next_token();

            int right = expression();
            DEBUG_LOG("Right side of relation: " << right);

            // Evaluate comparison
            switch (op) {
                case Tokenizer::TokenType::EQUAL:
                    return left == right;
                case Tokenizer::TokenType::LT:
                    return left < right;
                case Tokenizer::TokenType::GT:
                    return left > right;
                case Tokenizer::TokenType::LT_EQ:
                    return left <= right;
                case Tokenizer::TokenType::GT_EQ:
                    return left >= right;
                case Tokenizer::TokenType::NOT_EQUAL:
                    return left != right;
                default:
                    dprintf("Internal Error: Invalid comparison operator",
                            E_ERROR);
                    return 0;
            }
        }

        default:
            // No comparison operator - treat non-zero as true
            return left != 0;
    }
}

/**
 * Executes a LET statement.
 * Format: LET variable = expression
 *
 * @throws std::runtime_error on syntax errors
 */
void SUBARUU::let_statement() {
    DEBUG_LOG("Processing LET statement");

    // Get variable name
    auto token = tokenizer_->current_token();
    if (token != Tokenizer::TokenType::LETTER) {
        DEBUG_LOG("Expected LETTER, got: " << get_token_string(token));
        dprintf("Syntax Error: Expected variable name", E_ERROR);
        return;
    }

// Process variable
#ifdef DEBUG_MODE
    int var_index = tokenizer_->variable_num();
    char var_name = std::tolower(std::get<char>(tokenizer_->get_token_data()));
    DEBUG_LOG("Variable name: " << var_name << " (index: " << var_index << ")");
#else
    char var_name = std::tolower(std::get<char>(tokenizer_->get_token_data()));
#endif

    tokenizer_->next_token();

    // Verify and consume equals sign
    accept(Tokenizer::TokenType::EQUAL);

    // Evaluate the expression
    DEBUG_LOG("Evaluating expression");
    int value = expression();

    // Store the value
    variables_[var_name] = value;

#ifdef DEBUG_MODE
    DEBUG_LOG("Stored value " << value << " in variable " << var_name
                              << " at index " << var_index);
#endif
}
/**
 * Executes an IF statement.
 * Format: IF condition THEN line_number
 *
 * @throws std::runtime_error on syntax errors
 */

void SUBARUU::if_statement() {
    DEBUG_LOG("Processing IF statement");
    accept(Tokenizer::TokenType::IF);
    int condition = relation();
    DEBUG_LOG("Condition result: " << condition);
    accept(Tokenizer::TokenType::THEN);

    if (tokenizer_->current_token() != Tokenizer::TokenType::NUMBER) {
        dprintf("Syntax Error: Expected line number after THEN", E_ERROR);
        return;
    }

    int line_number = tokenizer_->get_num();
    tokenizer_->next_token();

    if (condition) {
        DEBUG_LOG("Condition true, jumping to line " << line_number);
        if (line_positions_.find(line_number) == line_positions_.end()) {
            dprintf("Runtime Error: Line number " +
                      std::to_string(line_number) + " not found",
                    E_ERROR);
            return;
        }

        tokenizer_->reset();
        if (!find_target_line(line_number)) {
            dprintf("Internal Error: Failed to find valid line number " +
                      std::to_string(line_number),
                    E_ERROR);
        }
    } else {
        // If condition is false, continue to next statement
        if (tokenizer_->current_token() == Tokenizer::TokenType::EOL) {
            tokenizer_->next_token();
        }
    }
}

/**
 * Executes a GOTO statement.
 * Format: GOTO line_number
 */

void SUBARUU::goto_statement() {
    accept(Tokenizer::TokenType::GOTO);
    int line_number = tokenizer_->get_num();
    accept(Tokenizer::TokenType::NUMBER);
    accept(Tokenizer::TokenType::EOL);

    if (line_positions_.find(line_number) == line_positions_.end()) {
        dprintf("Runtime Error: Line number " + std::to_string(line_number) +
                  " not found",
                E_ERROR);
        return;
    }

    tokenizer_->reset();
    if (!find_target_line(line_number)) {
        dprintf("Internal Error: Failed to find valid line number " +
                  std::to_string(line_number),
                E_ERROR);
    }
}

/**
 * Skips through the code until finding a specific line number.
 * Assumes tokenizer has been reset to beginning.
 *
 * @param line_number The target line number to find
 * @return bool True if line was found, false if reached end without finding
 */
bool SUBARUU::find_target_line(int line_number) {
    while (!tokenizer_->finished()) {
        if (tokenizer_->current_token() == Tokenizer::TokenType::NUMBER &&
            tokenizer_->get_num() == line_number) {
            tokenizer_->next_token(); // Skip past the line number
            return true;
        }

        // Skip to end of current line
        while (!tokenizer_->finished() &&
               tokenizer_->current_token() != Tokenizer::TokenType::EOL) {
            tokenizer_->next_token();
        }

        if (tokenizer_->current_token() == Tokenizer::TokenType::EOL) {
            tokenizer_->next_token();
        }
    }
    return false;
}

/**
 * Executes a PRINT statement.
 * Format: PRINT [expression|string|separator]...
 */
void SUBARUU::print_statement() {
    DEBUG_LOG("Entering print_statement");
    accept(Tokenizer::TokenType::PRINT);
    bool need_space = false;
    while (!tokenizer_->finished()) {
        auto token = tokenizer_->current_token();
        DEBUG_LOG("Print token: " << get_token_string(token));
        // Check for statement end
        if (is_statement_end(token)) {
            break;
        }
        // Check for line number
        if (is_line_number()) {
            break;
        }
        // Process print items
        switch (token) {
            case Tokenizer::TokenType::STRING:
                if (need_space) {
                    std::cout << " ";
                }
                std::cout << tokenizer_->get_string();
                need_space = true;
                tokenizer_->next_token();
                break;
            case Tokenizer::TokenType::SEPARATOR:
                need_space = false; // Reset need_space since we're using comma
                std::cout << " ";   // Single space after the previous item
                tokenizer_->next_token();
                break;
            case Tokenizer::TokenType::LETTER:
            case Tokenizer::TokenType::NUMBER:
            case Tokenizer::TokenType::LEFT_PAREN:
                if (need_space) {
                    std::cout << " ";
                }
                std::cout << expression();
                need_space = true;
                break;
            default:
                DEBUG_LOG(
                  "Found unexpected token: " << get_token_string(token));
                goto end_print;
        }
    }
end_print:
    std::cout << std::endl;
    auto final_token = tokenizer_->current_token();
    DEBUG_LOG("End of print, final token: " << get_token_string(final_token));
    if (is_line_number()) {
        DEBUG_LOG("Stopping at line number: " << tokenizer_->get_num());
        return;
    }
    // Handle normal statement endings
    if (final_token == Tokenizer::TokenType::EOF_TOKEN) {
        execution_finished_ = true;
    } else if (final_token == Tokenizer::TokenType::EOL) {
        tokenizer_->next_token();
    }
}

/**
 * Checks if the current token indicates end of statement
 */
bool SUBARUU::is_statement_end(Tokenizer::TokenType token) const {
    return token == Tokenizer::TokenType::EOL ||
           token == Tokenizer::TokenType::EOF_TOKEN;
}

/**
 * Checks if current token is a valid line number
 */
bool SUBARUU::is_line_number() const {
    if (tokenizer_->current_token() != Tokenizer::TokenType::NUMBER) {
        return false;
    }
    int num = tokenizer_->get_num();
    if (num >= 10 && num % 10 == 0) {
        char next_char = tokenizer_->peek_char();
        return (next_char == ' ' || next_char == '\n' || next_char == '\r');
    }
    return false;
}

/**
 * Executes a statement based on the current token.
 * Handles REM, PRINT, IF, GOTO, and LET statements.
 *
 * @throws std::runtime_error on syntax errors
 */
void SUBARUU::statement() {
    auto token = tokenizer_->current_token();
    DEBUG_LOG("Processing statement with token: " << get_token_string(token));
    switch (token) {
        case Tokenizer::TokenType::REM:
            DEBUG_LOG("Found REM statement");
            tokenizer_->skip_to_eol();
            break;
        case Tokenizer::TokenType::PRINT:
            DEBUG_LOG("Found PRINT statement");
            print_statement();
            break;
        case Tokenizer::TokenType::IF:
            DEBUG_LOG("Found IF statement");
            if_statement();
            break;
        case Tokenizer::TokenType::GOTO:
            DEBUG_LOG("Found GOTO statement");
            goto_statement();
            break;
        case Tokenizer::TokenType::LET:
            DEBUG_LOG("Found LET statement");
            accept(Tokenizer::TokenType::LET);
            [[fallthrough]];
        case Tokenizer::TokenType::LETTER:
            DEBUG_LOG("Found assignment statement");
            let_statement();
            break;
        default:
            DEBUG_LOG(
              "Unrecognized statement type: " << get_token_string(token));
            dprintf("Syntax Error: Unrecognized statement", E_ERROR);
            break;
    }
}

/**
 * Parses and executes a line statement.
 * Handles line numbers and statement execution.
 */

void SUBARUU::line_statement() {
    DEBUG_LOG("Starting line_statement");
    // Skip empty lines
    while (tokenizer_->current_token() == Tokenizer::TokenType::EOL) {
        tokenizer_->next_token();
    }
    // Check for end of file
    if (tokenizer_->current_token() == Tokenizer::TokenType::EOF_TOKEN) {
        execution_finished_ = true;
        return;
    }

    if (tokenizer_->current_token() == Tokenizer::TokenType::NUMBER) {
        tokenizer_->next_token(); // Move past line number
    }

    statement();
}

/**
 * Builds a map of line numbers in the program.
 * Maps each line number to its position in the source.
 */
void SUBARUU::build_line_map() {
    DEBUG_LOG("Building line number map");
    line_positions_.clear();
    tokenizer_->reset();
    std::unordered_map<int, bool> found_lines;
    // Scan through tokens looking for line numbers
    while (!tokenizer_->finished()) {
        auto token = tokenizer_->current_token();
        DEBUG_LOG("Map building - current token: " << get_token_string(token));

        if (token == Tokenizer::TokenType::NUMBER) {
            int value = tokenizer_->get_num();
            if (is_valid_line_number(value)) {
                line_positions_[value] = true;
                found_lines[value] = true;
                DEBUG_LOG("Found line number: " << value);
            }
        }
        tokenizer_->next_token();
    }
#ifdef DEBUG_MODE
    log_found_line_numbers(found_lines);
#endif
    tokenizer_->reset();
}

#ifdef DEBUG_MODE
/**
 * Helper to log all found line numbers during map building
 */
void SUBARUU::log_found_line_numbers(
  const std::unordered_map<int, bool>& found_lines) {
    std::string line_numbers;
    for (const auto& [line, _] : found_lines) {
        if (!line_numbers.empty()) {
            line_numbers += " ";
        }
        line_numbers += std::to_string(line);
    }
    DEBUG_LOG("Found these line numbers: " << line_numbers);
}
#endif
/**
 * Finds a specific line number in the source code.
 * Builds line map if not already built.
 *
 * @param linenum The line number to find
 * @throws std::runtime_error if line number not found
 */

void SUBARUU::find_linenum(int linenum) {
    DEBUG_LOG("Searching for line number: " << linenum);

    // Build line map if needed
    if (line_positions_.empty()) {
        build_line_map();
    }

    // Check if line exists in map
    if (line_positions_.find(linenum) == line_positions_.end()) {
        std::string error = "Runtime Error: Line number " +
                            std::to_string(linenum) + " not found";
        dprintf(error, E_ERROR);
        return;
    }

    // Set up skip mode
    skip_to_line_ = true;
    target_line_ = linenum;
}

/**
 * Jumps to a specific line number.
 * Sets up line search and initiates the search operation.
 *
 * @param linenum The line number to jump to
 */
void SUBARUU::jump_linenum(int linenum) {
    DEBUG_LOG("Attempting to jump to line " << linenum);
    if (line_positions_.empty()) {
        build_line_map();
    }

    // Reset tokenizer to start
    tokenizer_->reset();

    // Search for target line
    while (!tokenizer_->finished()) {
        auto token = tokenizer_->current_token();
        if (token == Tokenizer::TokenType::NUMBER) {
            int current_line = tokenizer_->get_num();
            if (current_line == linenum) {
                tokenizer_->next_token(); // Move past the line number
                return;                   // Found our line, ready to execute
            }
        }
        tokenizer_->next_token();
    }

    // If we get here, line wasn't found
    std::string error =
      "Runtime Error: Line number " + std::to_string(linenum) + " not found";
    dprintf(error, E_ERROR);
}

#ifdef DEBUG_MODE
/**
 * Helper to log available line numbers when target not found
 */
void SUBARUU::log_available_lines(int target_line) {
    DEBUG_LOG("Line " << target_line << " not found in map. Available lines:");
    for (const auto& [line, _] : line_positions_) {
        DEBUG_LOG(" " << line);
    }
}
#endif
