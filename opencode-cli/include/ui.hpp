#ifndef UI_HPP
#define UI_HPP

#include <string>
#include <vector>

namespace opencode {

class Ui {
public:
    Ui();
    
    void print_header();
    void print_welcome();
    std::string get_user_input(const std::string& prompt);
    void print_message(const std::string& message, const std::string& role = "assistant");
    void print_error(const std::string& error);
    void clear_screen();
    
    // Menu helpers
    int show_menu(const std::string& title, const std::vector<std::string>& options);
    std::string get_text_input(const std::string& prompt);
    
private:
    void print_utf8_border(const std::string& text);
};

} // namespace opencode

#endif // UI_HPP
