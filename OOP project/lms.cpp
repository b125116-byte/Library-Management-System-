// =============================================================================
//  Library Management System  (simple OOP version, C++17)
//  Based on: "Library Management System - Abstract, Workflow & Flowchart"
//
//  Class design (from the project document)
//      User (abstract) -> Librarian
//                      -> Member (abstract) -> Student, Faculty
//      Book, Transaction, Library
//
//  System workflow (each step is documented in its own PDF)
//      1 Login   2 Role-based menu   3 Search book   4 Issue book
//      5 Return book   6 Update records (save)   7 Report & logout
//
//  Build:  g++ -std=c++17 -Wall -Wextra lms.cpp -o lms
//  First login:  User ID  L001   Password  admin123
// =============================================================================

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
//  Errors and small helpers
// ----------------------------------------------------------------------------
class LibraryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct EndOfInput {};  // thrown when input is closed (Ctrl+D / Ctrl+Z)

namespace util {
std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}
bool containsIgnoreCase(const std::string& text, const std::string& part) {
    return toLower(text).find(toLower(part)) != std::string::npos;
}
// Files use '|' between fields, so '|' is not allowed inside typed text.
std::string clean(std::string s) {
    std::replace(s.begin(), s.end(), '|', '/');
    return trim(s);
}
std::string fit(const std::string& s, std::size_t w) { return s.size() <= w ? s : s.substr(0, w - 1) + "~"; }
std::string money(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << v;
    return os.str();
}
int parseInt(const std::string& s) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0') throw std::invalid_argument("'" + s + "' is not a whole number");
    return static_cast<int>(v);
}
double parseDouble(const std::string& s) {
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (s.empty() || *end != '\0') throw std::invalid_argument("'" + s + "' is not a number");
    return v;
}
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in(line);
    while (std::getline(in, cur, '|')) out.push_back(cur);
    if (!line.empty() && line.back() == '|') out.push_back("");
    return out;
}
std::string join(const std::vector<std::string>& f) {
    std::string s;
    for (std::size_t i = 0; i < f.size(); ++i) s += (i ? "|" : "") + f[i];
    return s;
}
}  // namespace util

// ----------------------------------------------------------------------------
//  Date: stored as a day count, so "due = issue + 14" and "late = return - due"
//  are simple integer maths. Handles leap years.
// ----------------------------------------------------------------------------
class Date {
    long long days_;  // days since 1970-01-01
    explicit Date(long long d) : days_(d) {}

public:
    Date() : days_(0) {}
    static bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
    static bool isValid(int y, int m, int d) {
        static const int len[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (y < 1900 || y > 2200 || m < 1 || m > 12 || d < 1) return false;
        return d <= len[m - 1] + ((m == 2 && isLeap(y)) ? 1 : 0);
    }
    static Date fromYMD(int y, int m, int d) {
        if (!isValid(y, m, d)) throw std::invalid_argument("invalid date");
        long long yy = y - (m <= 2 ? 1 : 0);
        long long era = (yy >= 0 ? yy : yy - 399) / 400;
        long long yoe = yy - era * 400;
        long long doy = (153LL * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return Date(era * 146097 + doe - 719468);
    }
    static Date today() {
        std::time_t t = std::time(nullptr);
        std::tm* lt = std::localtime(&t);
        return fromYMD(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    }
    static bool parse(const std::string& s, Date& out) {
        int y = 0, m = 0, d = 0;
        char a = 0, b = 0;
        std::istringstream in(s);
        if (!(in >> y >> a >> m >> b >> d) || a != '-' || b != '-') return false;
        in >> std::ws;
        if (!in.eof() || !isValid(y, m, d)) return false;
        out = fromYMD(y, m, d);
        return true;
    }
    std::string toString() const {
        long long z = days_ + 719468;
        long long era = (z >= 0 ? z : z - 146096) / 146097;
        long long doe = z - era * 146097;
        long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long long mp = (5 * doy + 2) / 153;
        int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
        int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
        int y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
        std::ostringstream os;
        os << std::setfill('0') << std::setw(4) << y << '-' << std::setw(2) << m << '-' << std::setw(2) << d;
        return os.str();
    }
    Date operator+(int n) const { return Date(days_ + n); }
    long long operator-(const Date& o) const { return days_ - o.days_; }
    bool operator<(const Date& o) const { return days_ < o.days_; }
    bool operator>(const Date& o) const { return o < *this; }
    bool operator<=(const Date& o) const { return !(o < *this); }
    bool operator>=(const Date& o) const { return !(*this < o); }
};

class Library;      // forward declarations: used by User/Librarian before their definitions
class Transaction;

// ----------------------------------------------------------------------------
//  Book
// ----------------------------------------------------------------------------
class Book {
public:
    enum class Status { Available, Issued };

private:
    int bookId_;
    std::string title_, author_, isbn_, category_;
    Status status_;

public:
    Book(int id, std::string title, std::string author, std::string isbn, std::string category,
         Status status = Status::Available)
        : bookId_(id), title_(std::move(title)), author_(std::move(author)), isbn_(std::move(isbn)),
          category_(std::move(category)), status_(status) {
        if (title_.empty() || author_.empty() || isbn_.empty() || category_.empty())
            throw std::invalid_argument("Title, author, ISBN and category are all required.");
    }
    int id() const { return bookId_; }
    const std::string& title() const { return title_; }
    const std::string& author() const { return author_; }
    const std::string& isbn() const { return isbn_; }
    const std::string& category() const { return category_; }
    bool isAvailable() const { return status_ == Status::Available; }
    void updateStatus(Status s) { status_ = s; }
    std::string statusText() const { return isAvailable() ? "Available" : "Issued"; }

    std::vector<std::string> toFields() const {
        return {std::to_string(bookId_), title_, author_, isbn_, category_, isAvailable() ? "0" : "1"};
    }
    static Book fromFields(const std::vector<std::string>& f) {
        if (f.size() != 6) throw std::invalid_argument("book record needs 6 fields");
        return Book(util::parseInt(f[0]), f[1], f[2], f[3], f[4],
                    f[5] == "1" ? Status::Issued : Status::Available);
    }
};

std::ostream& operator<<(std::ostream& os, const Book& b) {  // one table row
    return os << std::left << std::setw(4) << b.id() << std::setw(26) << util::fit(b.title(), 25)
              << std::setw(20) << util::fit(b.author(), 19) << std::setw(14) << util::fit(b.isbn(), 13)
              << std::setw(14) << util::fit(b.category(), 13) << b.statusText();
}
const char* bookHeader() { return "ID  Title                     Author              ISBN          Category      Status"; }

// ----------------------------------------------------------------------------
//  User (abstract) -> Librarian, Member (abstract) -> Student, Faculty
// ----------------------------------------------------------------------------
class User {
    std::string userId_, name_, email_;
    std::string password_;  // private: only checked through login()

public:
    User(std::string id, std::string name, std::string email, std::string password)
        : userId_(std::move(id)), name_(std::move(name)), email_(std::move(email)),
          password_(std::move(password)) {
        if (userId_.empty() || name_.empty() || email_.empty() || password_.empty())
            throw std::invalid_argument("ID, name, email and password are all required.");
    }
    virtual ~User() = default;

    const std::string& id() const { return userId_; }
    const std::string& name() const { return name_; }
    const std::string& email() const { return email_; }

    bool login(const std::string& password) const { return password == password_; }
    void logout() const { std::cout << "  " << name_ << " logged out.\n"; }
    std::vector<const Book*> searchBook(const Library& lib, const std::string& keyword) const;

    virtual std::string role() const = 0;
    virtual char typeCode() const = 0;
    virtual std::vector<std::string> toFields() const {  // one line of users.txt
        return {std::string(1, typeCode()), userId_, name_, email_, password_, "0.00"};
    }

protected:
    // Only subclasses (when saving) may read the password; it is never printed.
    const std::string& storedPassword() const { return password_; }
};

class Member : public User {
    double fine_ = 0.0;
    std::vector<int> booksIssued_;  // IDs of books currently held

public:
    using User::User;

    // Polymorphic rules: Student and Faculty override these.
    virtual int getBorrowLimit() const = 0;
    virtual int getLoanDays() const = 0;
    virtual double calculateFine(long long overdueDays) const = 0;

    double fine() const { return fine_; }
    void setFine(double f) { fine_ = f; }
    void addFine(double f) { fine_ += f; }
    const std::vector<int>& booksIssued() const { return booksIssued_; }
    bool holds(int bookId) const {
        return std::find(booksIssued_.begin(), booksIssued_.end(), bookId) != booksIssued_.end();
    }
    void borrowBook(int bookId) {
        if (static_cast<int>(booksIssued_.size()) >= getBorrowLimit())
            throw LibraryError(role() + " limit reached (" + std::to_string(getBorrowLimit()) + " books).");
        booksIssued_.push_back(bookId);
    }
    void returnBook(int bookId) {
        booksIssued_.erase(std::remove(booksIssued_.begin(), booksIssued_.end(), bookId), booksIssued_.end());
    }
    void payFine(double amount) {
        if (amount <= 0) throw LibraryError("Payment must be greater than zero.");
        if (amount > fine_ + 1e-9) throw LibraryError("Payment is more than the fine due (" + util::money(fine_) + ").");
        fine_ -= amount;
        if (fine_ < 0.005) fine_ = 0.0;
    }
    std::vector<std::string> toFields() const override {
        return {std::string(1, typeCode()), id(), name(), email(), storedPassword(), util::money(fine_)};
    }
};

class Student : public Member {
public:
    using Member::Member;
    std::string role() const override { return "Student"; }
    char typeCode() const override { return 'S'; }
    int getBorrowLimit() const override { return 3; }
    int getLoanDays() const override { return 14; }
    double calculateFine(long long d) const override { return d > 0 ? static_cast<double>(d) * 2.0 : 0.0; }
};

class Faculty : public Member {
public:
    using Member::Member;
    std::string role() const override { return "Faculty"; }
    char typeCode() const override { return 'F'; }
    int getBorrowLimit() const override { return 5; }
    int getLoanDays() const override { return 30; }
    double calculateFine(long long d) const override { return d > 0 ? static_cast<double>(d) * 1.0 : 0.0; }
};

class Librarian : public User {
public:
    using User::User;
    std::string role() const override { return "Librarian"; }
    char typeCode() const override { return 'L'; }

    // Only a Librarian object has these methods -> role-based access by design.
    const Book& addBook(Library& lib, const std::string& title, const std::string& author,
                        const std::string& isbn, const std::string& category) const;
    void removeBook(Library& lib, int bookId) const;
    std::string registerMember(Library& lib, char type, const std::string& name, const std::string& email,
                               const std::string& password) const;
    Transaction issueBook(Library& lib, const std::string& memberId, int bookId, Date issueDate) const;
    Transaction acceptReturn(Library& lib, const std::string& memberId, int bookId, Date returnDate) const;
};

// ----------------------------------------------------------------------------
//  Transaction: links one Member with one Book
// ----------------------------------------------------------------------------
class Transaction {
    int transId_, bookId_;
    std::string memberId_;
    Date issueDate_, dueDate_, returnDate_;
    bool closed_ = false;
    double fine_ = 0.0;
    int renewals_ = 0;

public:
    Transaction(int id, int bookId, std::string memberId, Date issue, Date due)
        : transId_(id), bookId_(bookId), memberId_(std::move(memberId)), issueDate_(issue), dueDate_(due) {}

    int id() const { return transId_; }
    int bookId() const { return bookId_; }
    const std::string& memberId() const { return memberId_; }
    Date issueDate() const { return issueDate_; }
    Date dueDate() const { return dueDate_; }
    Date returnDate() const { return returnDate_; }
    bool isClosed() const { return closed_; }
    double fine() const { return fine_; }
    int renewals() const { return renewals_; }

    long long overdueDays(Date asOf) const {
        long long d = (closed_ ? returnDate_ : asOf) - dueDate_;
        return d > 0 ? d : 0;
    }
    bool isOverdue(Date asOf) const { return !closed_ && asOf > dueDate_; }

    // The fine rule belongs to the member type (polymorphism).
    double calculateFine(const Member& m, Date returnedOn) const {
        long long late = returnedOn - dueDate_;
        return m.calculateFine(late > 0 ? late : 0);
    }
    void closeTransaction(Date when, double fine) { closed_ = true; returnDate_ = when; fine_ = fine; }
    void renew(int days) { dueDate_ = dueDate_ + days; ++renewals_; }

    std::vector<std::string> toFields() const {
        return {std::to_string(transId_), std::to_string(bookId_), memberId_, issueDate_.toString(),
                dueDate_.toString(), closed_ ? "1" : "0", closed_ ? returnDate_.toString() : "-",
                util::money(fine_), std::to_string(renewals_)};
    }
    static Transaction fromFields(const std::vector<std::string>& f) {
        if (f.size() != 9) throw std::invalid_argument("transaction record needs 9 fields");
        Date issue, due, ret;
        if (!Date::parse(f[3], issue) || !Date::parse(f[4], due)) throw std::invalid_argument("bad date");
        Transaction t(util::parseInt(f[0]), util::parseInt(f[1]), f[2], issue, due);
        t.renewals_ = util::parseInt(f[8]);
        if (f[5] == "1") {
            if (!Date::parse(f[6], ret)) throw std::invalid_argument("bad return date");
            t.closeTransaction(ret, util::parseDouble(f[7]));
        }
        return t;
    }
};

// ----------------------------------------------------------------------------
//  Library: contains Books, Users and Transactions (composition)
// ----------------------------------------------------------------------------
class Library {
public:
    enum class SearchField { All, Title, Author, Isbn, Category };

private:
    std::vector<Book> books_;
    std::vector<std::unique_ptr<User>> users_;
    std::vector<Transaction> transactions_;
    int nextBookId_ = 1, nextTransId_ = 1;
    static constexpr int kMaxRenewals = 1;

    Book* findBookMutable(int id) {
        auto it = std::find_if(books_.begin(), books_.end(), [&](const Book& b) { return b.id() == id; });
        return it == books_.end() ? nullptr : &*it;
    }
    Transaction* findActive(const std::string& memberId, int bookId) {
        auto it = std::find_if(transactions_.begin(), transactions_.end(), [&](const Transaction& t) {
            return !t.isClosed() && t.memberId() == memberId && t.bookId() == bookId;
        });
        return it == transactions_.end() ? nullptr : &*it;
    }
    Member& needMember(const std::string& id) {
        Member* m = findMember(id);
        if (!m) throw LibraryError("Member '" + id + "' not found.");
        return *m;
    }

public:
    // ---- catalogue --------------------------------------------------------
    const Book& addBook(const std::string& title, const std::string& author, const std::string& isbn,
                        const std::string& category) {
        Book b(nextBookId_, title, author, isbn, category);
        books_.push_back(b);
        ++nextBookId_;
        return books_.back();
    }
    void removeBook(int id) {
        Book* b = findBookMutable(id);
        if (!b) throw LibraryError("Book " + std::to_string(id) + " not found.");
        if (!b->isAvailable()) throw LibraryError("Cannot remove: the book is currently issued.");
        books_.erase(std::remove_if(books_.begin(), books_.end(), [&](const Book& x) { return x.id() == id; }),
                     books_.end());
    }
    const Book* findBook(int id) const {
        auto it = std::find_if(books_.begin(), books_.end(), [&](const Book& b) { return b.id() == id; });
        return it == books_.end() ? nullptr : &*it;
    }
    const std::vector<Book>& books() const { return books_; }

    // Workflow step 3: search by title / author / ISBN / category
    std::vector<const Book*> search(const std::string& keyword, SearchField field) const {
        std::vector<const Book*> found;
        for (const Book& b : books_) {
            bool hit = false;
            if (field == SearchField::All || field == SearchField::Title) hit |= util::containsIgnoreCase(b.title(), keyword);
            if (field == SearchField::All || field == SearchField::Author) hit |= util::containsIgnoreCase(b.author(), keyword);
            if (field == SearchField::All || field == SearchField::Isbn) hit |= util::containsIgnoreCase(b.isbn(), keyword);
            if (field == SearchField::All || field == SearchField::Category) hit |= util::containsIgnoreCase(b.category(), keyword);
            if (hit) found.push_back(&b);
        }
        return found;
    }

    // ---- users ------------------------------------------------------------
    void addUser(std::unique_ptr<User> u) {
        if (findUser(u->id())) throw LibraryError("User ID '" + u->id() + "' already exists.");
        users_.push_back(std::move(u));
    }
    User* findUser(const std::string& id) const {
        auto it = std::find_if(users_.begin(), users_.end(), [&](const std::unique_ptr<User>& u) { return u->id() == id; });
        return it == users_.end() ? nullptr : it->get();
    }
    Member* findMember(const std::string& id) const { return dynamic_cast<Member*>(findUser(id)); }
    std::size_t userCount() const { return users_.size(); }

    // Workflow step 1: verify credentials against stored records
    User* authenticate(const std::string& id, const std::string& password) const {
        User* u = findUser(id);
        return (u && u->login(password)) ? u : nullptr;
    }

    std::string registerMember(char type, const std::string& name, const std::string& email,
                               const std::string& password) {
        if (type != 'S' && type != 'F') throw LibraryError("Member type must be S (Student) or F (Faculty).");
        int n = 1;
        std::string id;
        do {
            std::ostringstream os;
            os << type << std::setfill('0') << std::setw(3) << n++;
            id = os.str();
        } while (findUser(id));
        if (type == 'S') addUser(std::make_unique<Student>(id, name, email, password));
        else addUser(std::make_unique<Faculty>(id, name, email, password));
        return id;
    }

    // ---- workflow step 4: issue ------------------------------------------------
    Transaction issueBook(const std::string& memberId, int bookId, Date issueDate) {
        Member& m = needMember(memberId);
        Book* b = findBookMutable(bookId);
        if (!b) throw LibraryError("Book " + std::to_string(bookId) + " not found.");
        if (issueDate > Date::today()) throw LibraryError("Issue date cannot be in the future.");
        if (!b->isAvailable()) throw LibraryError("'" + b->title() + "' is already issued.");
        if (m.fine() > 0) throw LibraryError("Member has an unpaid fine of " + util::money(m.fine()) + ". Pay it first.");

        Transaction t(nextTransId_, bookId, memberId, issueDate, issueDate + m.getLoanDays());
        m.borrowBook(bookId);  // throws if over the limit (nothing changed yet)
        b->updateStatus(Book::Status::Issued);
        ++nextTransId_;
        transactions_.push_back(t);
        return t;
    }

    // ---- workflow step 5: return -------------------------------------------------
    Transaction returnBook(const std::string& memberId, int bookId, Date returnDate) {
        Member& m = needMember(memberId);
        Transaction* t = findActive(memberId, bookId);
        if (!t) throw LibraryError("No active loan for this member and book (already returned?).");
        if (returnDate < t->issueDate()) throw LibraryError("Return date cannot be before the issue date.");
        if (returnDate > Date::today()) throw LibraryError("Return date cannot be in the future.");

        double fine = t->calculateFine(m, returnDate);
        t->closeTransaction(returnDate, fine);
        m.returnBook(bookId);
        m.addFine(fine);
        if (Book* b = findBookMutable(bookId)) b->updateStatus(Book::Status::Available);
        return *t;
    }

    // ---- renewal -----------------------------------------------------------------
    Transaction renewBook(const std::string& memberId, int bookId, Date asOf) {
        Member& m = needMember(memberId);
        Transaction* t = findActive(memberId, bookId);
        if (!t) throw LibraryError("You do not have this book issued.");
        if (t->isOverdue(asOf)) throw LibraryError("Overdue books cannot be renewed. Return it first.");
        if (t->renewals() >= kMaxRenewals) throw LibraryError("This book has already been renewed once.");
        t->renew(m.getLoanDays());
        return *t;
    }

    // ---- queries and reports -------------------------------------------------------
    std::vector<const Transaction*> activeLoans() const {
        std::vector<const Transaction*> v;
        for (const auto& t : transactions_) if (!t.isClosed()) v.push_back(&t);
        return v;
    }
    std::vector<const Transaction*> activeLoansOf(const std::string& memberId) const {
        std::vector<const Transaction*> v;
        for (const auto& t : transactions_) if (!t.isClosed() && t.memberId() == memberId) v.push_back(&t);
        return v;
    }
    std::string bookTitle(int id) const { const Book* b = findBook(id); return b ? b->title() : "[removed]"; }

    // Workflow step 7: report
    void generateReport(std::ostream& os, Date today) const {
        int avail = 0, students = 0, faculty = 0, overdue = 0;
        double pending = 0, collected = 0;
        for (const Book& b : books_) avail += b.isAvailable();
        for (const auto& u : users_) {
            if (const Member* m = dynamic_cast<const Member*>(u.get())) {
                (m->typeCode() == 'S' ? students : faculty)++;
                pending += m->fine();
            }
        }
        for (const auto& t : transactions_) {
            if (t.isClosed()) collected += t.fine();
            else if (t.isOverdue(today)) ++overdue;
        }
        os << "\n=========== LIBRARY REPORT (" << today.toString() << ") ===========\n"
           << "  Book titles        : " << books_.size() << "\n"
           << "  Available / Issued : " << avail << " / " << (books_.size() - avail) << "\n"
           << "  Members            : " << students << " student(s), " << faculty << " faculty\n"
           << "  Active loans       : " << activeLoans().size() << "\n"
           << "  Overdue loans      : " << overdue << "\n"
           << "  Unpaid member fines: " << util::money(pending) << "\n"
           << "  Fines charged total: " << util::money(collected) << "\n";
        if (overdue) {
            os << "\n  Overdue list:\n";
            for (const auto& t : transactions_)
                if (t.isOverdue(today))
                    os << "   Txn #" << t.id() << "  " << util::fit(bookTitle(t.bookId()), 24) << "  member " << t.memberId()
                       << "  due " << t.dueDate().toString() << "  (" << t.overdueDays(today) << " days late)\n";
        }
    }

    // ---- workflow step 6: update records (save / load) -------------------------------
    void save() const {
        write("books.txt", [&](std::vector<std::string>& out) {
            for (const Book& b : books_) out.push_back(util::join(b.toFields()));
        });
        write("users.txt", [&](std::vector<std::string>& out) {
            for (const auto& u : users_) out.push_back(util::join(u->toFields()));
        });
        write("transactions.txt", [&](std::vector<std::string>& out) {
            for (const Transaction& t : transactions_) out.push_back(util::join(t.toFields()));
        });
    }

    // Returns warnings for any record that could not be read.
    std::vector<std::string> load() {
        std::vector<std::string> warnings;
        read("books.txt", warnings, [&](const std::vector<std::string>& f) {
            Book b = Book::fromFields(f);
            nextBookId_ = std::max(nextBookId_, b.id() + 1);
            books_.push_back(b);
        });
        read("users.txt", warnings, [&](const std::vector<std::string>& f) {
            if (f.size() != 6 || f[0].size() != 1) throw std::invalid_argument("user record needs 6 fields");
            std::unique_ptr<User> u;
            if (f[0] == "L") u = std::make_unique<Librarian>(f[1], f[2], f[3], f[4]);
            else if (f[0] == "S") u = std::make_unique<Student>(f[1], f[2], f[3], f[4]);
            else if (f[0] == "F") u = std::make_unique<Faculty>(f[1], f[2], f[3], f[4]);
            else throw std::invalid_argument("unknown user type");
            if (Member* m = dynamic_cast<Member*>(u.get())) m->setFine(util::parseDouble(f[5]));
            addUser(std::move(u));
        });
        read("transactions.txt", warnings, [&](const std::vector<std::string>& f) {
            Transaction t = Transaction::fromFields(f);
            nextTransId_ = std::max(nextTransId_, t.id() + 1);
            transactions_.push_back(t);
        });
        rebuildLinks();
        return warnings;
    }

private:
    // Member/book status are rebuilt from the open transactions, so they always agree.
    void rebuildLinks() {
        for (Book& b : books_) b.updateStatus(Book::Status::Available);
        for (const Transaction& t : transactions_) {
            if (t.isClosed()) continue;
            Member* m = findMember(t.memberId());
            Book* b = findBookMutable(t.bookId());
            if (m && b) { b->updateStatus(Book::Status::Issued); m->borrowBook(t.bookId()); }
        }
    }
    static void write(const std::string& file, const std::function<void(std::vector<std::string>&)>& fill) {
        std::vector<std::string> lines;
        fill(lines);
        std::ofstream out(file, std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open '" + file + "' for writing");
        for (const auto& l : lines) out << l << '\n';
        if (!out) throw std::runtime_error("failed while writing '" + file + "'");
    }
    void read(const std::string& file, std::vector<std::string>& warnings,
              const std::function<void(const std::vector<std::string>&)>& handle) {
        std::ifstream in(file);  // a missing file just means "first run"
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            ++n;
            if (util::trim(line).empty()) continue;
            try { handle(util::split(line)); }
            catch (const std::exception& e) {
                warnings.push_back(file + " line " + std::to_string(n) + ": " + e.what() + " (skipped)");
            }
        }
    }
};

// ----------------------------------------------------------------------------
//  Methods declared earlier that need the full Library definition
// ----------------------------------------------------------------------------
std::vector<const Book*> User::searchBook(const Library& lib, const std::string& keyword) const {
    return lib.search(keyword, Library::SearchField::All);
}
const Book& Librarian::addBook(Library& lib, const std::string& title, const std::string& author,
                               const std::string& isbn, const std::string& category) const {
    return lib.addBook(title, author, isbn, category);
}
void Librarian::removeBook(Library& lib, int bookId) const { lib.removeBook(bookId); }
std::string Librarian::registerMember(Library& lib, char type, const std::string& name,
                                      const std::string& email, const std::string& password) const {
    return lib.registerMember(type, name, email, password);
}
Transaction Librarian::issueBook(Library& lib, const std::string& memberId, int bookId, Date d) const {
    return lib.issueBook(memberId, bookId, d);
}
Transaction Librarian::acceptReturn(Library& lib, const std::string& memberId, int bookId, Date d) const {
    return lib.returnBook(memberId, bookId, d);
}

// ----------------------------------------------------------------------------
//  Console: safe input (always getline, so numbers and text never clash)
// ----------------------------------------------------------------------------
class Console {
public:
    static std::string readLine(const std::string& prompt) {
        std::cout << prompt;
        std::string s;
        if (!std::getline(std::cin, s)) throw EndOfInput{};
        return util::trim(s);
    }
    static std::string readText(const std::string& prompt) {  // required, '|' removed
        while (true) {
            std::string s = util::clean(readLine(prompt));
            if (!s.empty()) return s;
            std::cout << "  This field cannot be empty.\n";
        }
    }
    static std::string readId(const std::string& prompt) { return util::toUpper(readText(prompt)); }
    static int readInt(const std::string& prompt, int lo, int hi) {
        while (true) {
            try {
                int v = util::parseInt(readLine(prompt));
                if (v >= lo && v <= hi) return v;
            } catch (const std::exception&) {}
            std::cout << "  Please enter a whole number from " << lo << " to " << hi << ".\n";
        }
    }
    static double readAmount(const std::string& prompt) {
        while (true) {
            try {
                double v = util::parseDouble(readLine(prompt));
                if (v > 0) return v;
            } catch (const std::exception&) {}
            std::cout << "  Please enter an amount greater than 0.\n";
        }
    }
    static Date readDateOrToday(const std::string& prompt) {
        while (true) {
            std::string s = readLine(prompt + " (YYYY-MM-DD, Enter = today): ");
            if (s.empty()) return Date::today();
            Date d;
            if (Date::parse(s, d)) return d;
            std::cout << "  Invalid date (check month, day and leap years).\n";
        }
    }
    static bool confirm(const std::string& prompt) {
        while (true) {
            std::string s = util::toLower(readLine(prompt + " (y/n): "));
            if (s == "y" || s == "yes") return true;
            if (s == "n" || s == "no") return false;
            std::cout << "  Please answer y or n.\n";
        }
    }
};

// ----------------------------------------------------------------------------
//  LibraryApp: login, role-based menus, and the workflow steps
// ----------------------------------------------------------------------------
class LibraryApp {
    Library lib_;

    // ---- helpers --------------------------------------------------------------
    void guarded(const std::function<void()>& action) {
        try { action(); }
        catch (const std::exception& e) { std::cout << "  [Error] " << e.what() << "\n"; }
    }
    // Workflow step 6: update records (write books, users and transactions to disk)
    void persist() {
        try { lib_.save(); }
        catch (const std::exception& e) { std::cout << "  [Warning] Could not save data: " << e.what() << "\n"; }
    }
    void printBooks(const std::vector<const Book*>& list) const {
        if (list.empty()) { std::cout << "  No books found.\n"; return; }
        std::cout << "\n" << bookHeader() << "\n" << std::string(86, '-') << "\n";
        for (const Book* b : list) std::cout << *b << "\n";
        std::cout << "(" << list.size() << " book(s))\n";
    }
    void printLoans(const std::vector<const Transaction*>& list) const {
        if (list.empty()) { std::cout << "  No active loans.\n"; return; }
        Date today = Date::today();
        std::cout << "\n" << std::left << std::setw(5) << "Txn" << std::setw(5) << "Book" << std::setw(24) << "Title"
                  << std::setw(8) << "Member" << std::setw(12) << "Issued" << std::setw(12) << "Due" << "Status\n"
                  << std::string(80, '-') << "\n";
        for (const Transaction* t : list) {
            std::string status = t->isOverdue(today) ? "OVERDUE " + std::to_string(t->overdueDays(today)) + "d"
                                                     : (t->renewals() ? "On loan (renewed)" : "On loan");
            std::cout << std::left << std::setw(5) << t->id() << std::setw(5) << t->bookId() << std::setw(24)
                      << util::fit(lib_.bookTitle(t->bookId()), 23) << std::setw(8) << t->memberId() << std::setw(12)
                      << t->issueDate().toString() << std::setw(12) << t->dueDate().toString() << status << "\n";
        }
    }
    Library::SearchField askSearchField() const {
        int c = Console::readInt("Search in  1) All  2) Title  3) Author  4) ISBN  5) Category: ", 1, 5);
        static const Library::SearchField f[] = {Library::SearchField::All, Library::SearchField::Title,
                                                 Library::SearchField::Author, Library::SearchField::Isbn,
                                                 Library::SearchField::Category};
        return f[c - 1];
    }

    // ---- workflow step 3: search ------------------------------------------------
    void searchBooks(const User& user) {
        Library::SearchField field = askSearchField();
        std::string keyword = Console::readText("Keyword: ");
        // 'All' uses User::searchBook; a specific field asks the library directly.
        printBooks(field == Library::SearchField::All ? user.searchBook(lib_, keyword) : lib_.search(keyword, field));
    }

    // ---- librarian operations -----------------------------------------------------
    void addBook(const Librarian& lb) {
        std::string title = Console::readText("Title: ");
        std::string author = Console::readText("Author: ");
        std::string isbn = Console::readText("ISBN: ");
        std::string category = Console::readText("Category: ");
        const Book& b = lb.addBook(lib_, title, author, isbn, category);
        persist();
        std::cout << "  Book added with ID " << b.id() << ".\n";
    }
    void removeBook(const Librarian& lb) {
        int id = Console::readInt("Book ID to remove: ", 1, 999999);
        const Book* b = lib_.findBook(id);
        if (!b) throw LibraryError("Book " + std::to_string(id) + " not found.");
        if (!b->isAvailable()) throw LibraryError("Cannot remove: the book is currently issued.");
        if (!Console::confirm("Remove '" + b->title() + "'?")) { std::cout << "  Cancelled.\n"; return; }
        lb.removeBook(lib_, id);
        persist();
        std::cout << "  Book removed.\n";
    }
    void registerMember(const Librarian& lb) {
        int type = Console::readInt("Member type  1) Student  2) Faculty: ", 1, 2);
        std::string name = Console::readText("Name: ");
        std::string email = Console::readText("Email: ");
        std::string password = Console::readText("Password for the member: ");
        std::string id = lb.registerMember(lib_, type == 1 ? 'S' : 'F', name, email, password);
        persist();
        std::cout << "  Member registered. Login ID: " << id << "\n";
    }
    void issueBook(const Librarian& lb) {
        std::string memberId = Console::readId("Member ID: ");
        int bookId = Console::readInt("Book ID: ", 1, 999999);
        Date when = Console::readDateOrToday("Issue date");
        Transaction t = lb.issueBook(lib_, memberId, bookId, when);
        persist();
        const Member* m = lib_.findMember(memberId);
        std::cout << "\n------------- ISSUE RECEIPT -------------\n"
                  << "  Transaction : #" << t.id() << "\n  Book        : [" << bookId << "] " << lib_.bookTitle(bookId) << "\n"
                  << "  Member      : " << memberId << " - " << m->name() << " (" << m->role() << ")\n"
                  << "  Issued on   : " << t.issueDate().toString() << "\n  Due date    : " << t.dueDate().toString() << "\n"
                  << "  Books held  : " << m->booksIssued().size() << " of " << m->getBorrowLimit() << "\n"
                  << "-----------------------------------------\n";
    }
    void acceptReturn(const Librarian& lb) {
        std::string memberId = Console::readId("Member ID: ");
        int bookId = Console::readInt("Book ID: ", 1, 999999);
        Date when = Console::readDateOrToday("Return date");
        Transaction t = lb.acceptReturn(lib_, memberId, bookId, when);
        persist();
        const Member* m = lib_.findMember(memberId);
        long long late = t.overdueDays(t.returnDate());
        std::cout << "\n------------ RETURN RECEIPT -------------\n"
                  << "  Transaction : #" << t.id() << "\n  Book        : [" << bookId << "] " << lib_.bookTitle(bookId) << "\n"
                  << "  Member      : " << memberId << " - " << m->name() << " (" << m->role() << ")\n"
                  << "  Due date    : " << t.dueDate().toString() << "\n  Returned on : " << t.returnDate().toString() << "\n";
        if (late > 0) std::cout << "  Overdue     : " << late << " day(s)\n  Fine        : " << util::money(t.fine()) << "\n";
        else std::cout << "  Returned on time. Fine: 0.00\n";
        std::cout << "  Member's unpaid fine: " << util::money(m->fine()) << "\n-----------------------------------------\n";
    }

    // ---- workflow step 2: role-based menus ------------------------------------------
    void librarianMenu(Librarian& lb) {
        while (true) {
            std::cout << "\n===== LIBRARIAN MENU =====\n 1. Add book\n 2. Remove book\n 3. Register member\n"
                         " 4. Issue book\n 5. Accept return\n 6. Search book\n 7. View all books\n"
                         " 8. View active loans\n 9. Generate report\n 0. Logout\n";
            int c = Console::readInt("Choice: ", 0, 9);
            if (c == 0) return;
            guarded([&] {
                switch (c) {
                    case 1: addBook(lb); break;
                    case 2: removeBook(lb); break;
                    case 3: registerMember(lb); break;
                    case 4: issueBook(lb); break;
                    case 5: acceptReturn(lb); break;
                    case 6: searchBooks(lb); break;
                    case 7: { std::vector<const Book*> all; for (const Book& b : lib_.books()) all.push_back(&b); printBooks(all); break; }
                    case 8: printLoans(lib_.activeLoans()); break;
                    case 9: lib_.generateReport(std::cout, Date::today()); break;
                }
            });
        }
    }
    void memberMenu(Member& m) {
        while (true) {
            std::cout << "\n===== MEMBER MENU (" << m.role() << ") =====\n 1. Search book\n 2. View my issued books\n"
                         " 3. Renew a book\n 4. Pay fine\n 0. Logout\n";
            int c = Console::readInt("Choice: ", 0, 4);
            if (c == 0) return;
            guarded([&] {
                switch (c) {
                    case 1: searchBooks(m); break;
                    case 2:
                        printLoans(lib_.activeLoansOf(m.id()));
                        std::cout << "  Borrow limit: " << m.booksIssued().size() << " of " << m.getBorrowLimit()
                                  << " used.  Unpaid fine: " << util::money(m.fine()) << "\n";
                        break;
                    case 3: {
                        int bookId = Console::readInt("Book ID to renew: ", 1, 999999);
                        Transaction t = lib_.renewBook(m.id(), bookId, Date::today());
                        persist();
                        std::cout << "  Renewed. New due date: " << t.dueDate().toString() << "\n";
                        break;
                    }
                    case 4: {
                        if (m.fine() <= 0) { std::cout << "  You have no unpaid fine.\n"; break; }
                        std::cout << "  Unpaid fine: " << util::money(m.fine()) << "\n";
                        m.payFine(Console::readAmount("Amount to pay: "));
                        persist();
                        std::cout << "  Payment recorded. Remaining fine: " << util::money(m.fine()) << "\n";
                        break;
                    }
                }
            });
        }
    }

    // ---- workflow step 1: login -------------------------------------------------------
    User* loginScreen() {
        std::cout << "\n========== LIBRARY MANAGEMENT SYSTEM ==========\n  (enter 0 as User ID to exit)\n";
        for (int attempts = 3; attempts > 0; --attempts) {
            std::string id = util::toUpper(Console::readLine("User ID : "));
            if (id == "0") return nullptr;
            std::string password = Console::readLine("Password: ");
            User* u = lib_.authenticate(id, password);
            if (u) return u;
            std::cout << "  [Error] Invalid user ID or password. " << (attempts - 1) << " attempt(s) left.\n";
        }
        std::cout << "  Too many failed attempts.\n";
        return nullptr;
    }

public:
    int run() {
        try {
            for (const auto& w : lib_.load()) std::cout << "[Warning] " << w << "\n";
            if (lib_.userCount() == 0) {  // first run: create a default librarian account
                lib_.addUser(std::make_unique<Librarian>("L001", "Head Librarian", "admin@library.local", "admin123"));
                persist();
                std::cout << "First run: default librarian created (ID L001, password admin123).\n";
            }
            while (User* user = loginScreen()) {
                std::cout << "\n  Welcome, " << user->name() << " (" << user->role() << ")\n";
                // Role-based menu: the object's real type decides which menu opens.
                if (auto* lb = dynamic_cast<Librarian*>(user)) librarianMenu(*lb);
                else if (auto* m = dynamic_cast<Member*>(user)) memberMenu(*m);
                persist();      // workflow step 7: save on logout
                user->logout();
            }
        } catch (const EndOfInput&) {
            std::cout << "\nInput closed. Saving and exiting...\n";
        }
        persist();
        std::cout << "Goodbye!\n";
        return 0;
    }
};

int main() {
    LibraryApp app;
    return app.run();
}