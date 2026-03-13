#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace std;

const int MAX_KEY_SIZE = 65;
const int ORDER = 100; // B+ tree order

struct Key {
    char data[MAX_KEY_SIZE];

    Key() { memset(data, 0, MAX_KEY_SIZE); }

    Key(const char* str) {
        memset(data, 0, MAX_KEY_SIZE);
        strncpy(data, str, MAX_KEY_SIZE - 1);
    }

    bool operator<(const Key& other) const {
        return strcmp(data, other.data) < 0;
    }

    bool operator==(const Key& other) const {
        return strcmp(data, other.data) == 0;
    }

    bool operator<=(const Key& other) const {
        return strcmp(data, other.data) <= 0;
    }

    bool operator>=(const Key& other) const {
        return strcmp(data, other.data) >= 0;
    }

    bool operator>(const Key& other) const {
        return strcmp(data, other.data) > 0;
    }
};

struct DataPair {
    Key key;
    int value;

    DataPair() : value(0) {}
    DataPair(const Key& k, int v) : key(k), value(v) {}

    bool operator<(const DataPair& other) const {
        if (key == other.key) return value < other.value;
        return key < other.key;
    }

    bool operator==(const DataPair& other) const {
        return key == other.key && value == other.value;
    }
};

struct Node {
    bool is_leaf;
    int num_keys;
    DataPair pairs[ORDER];
    int children[ORDER + 1];
    int next_leaf;

    Node() : is_leaf(true), num_keys(0), next_leaf(-1) {
        memset(children, -1, sizeof(children));
    }
};

class BPlusTree {
private:
    string filename;
    fstream file;
    int root;
    int node_count;

    int allocate_node() {
        return node_count++;
    }

    void write_node(int pos, const Node& node) {
        file.seekp(sizeof(int) * 2 + pos * sizeof(Node));
        file.write(reinterpret_cast<const char*>(&node), sizeof(Node));
        file.flush();
    }

    Node read_node(int pos) {
        Node node;
        file.seekg(sizeof(int) * 2 + pos * sizeof(Node));
        file.read(reinterpret_cast<char*>(&node), sizeof(Node));
        return node;
    }

    void write_metadata() {
        file.seekp(0);
        file.write(reinterpret_cast<const char*>(&root), sizeof(int));
        file.write(reinterpret_cast<const char*>(&node_count), sizeof(int));
        file.flush();
    }

    void read_metadata() {
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&root), sizeof(int));
        file.read(reinterpret_cast<char*>(&node_count), sizeof(int));
    }

    void split_child(int parent_pos, int index) {
        Node parent = read_node(parent_pos);
        Node child = read_node(parent.children[index]);

        Node new_child;
        new_child.is_leaf = child.is_leaf;

        int mid = ORDER / 2;
        new_child.num_keys = child.num_keys - mid;

        for (int i = 0; i < new_child.num_keys; i++) {
            new_child.pairs[i] = child.pairs[mid + i];
        }

        if (!child.is_leaf) {
            for (int i = 0; i <= new_child.num_keys; i++) {
                new_child.children[i] = child.children[mid + i];
            }
        } else {
            new_child.next_leaf = child.next_leaf;
            child.next_leaf = node_count;
        }

        child.num_keys = mid;

        int new_child_pos = allocate_node();
        write_node(new_child_pos, new_child);
        write_node(parent.children[index], child);

        for (int i = parent.num_keys; i > index; i--) {
            parent.children[i + 1] = parent.children[i];
            parent.pairs[i] = parent.pairs[i - 1];
        }

        parent.children[index + 1] = new_child_pos;
        parent.pairs[index] = new_child.pairs[0];
        parent.num_keys++;

        write_node(parent_pos, parent);
    }

    void insert_non_full(int node_pos, const DataPair& pair) {
        Node node = read_node(node_pos);
        int i = node.num_keys - 1;

        if (node.is_leaf) {
            while (i >= 0 && pair < node.pairs[i]) {
                node.pairs[i + 1] = node.pairs[i];
                i--;
            }
            node.pairs[i + 1] = pair;
            node.num_keys++;
            write_node(node_pos, node);
        } else {
            while (i >= 0 && pair.key < node.pairs[i].key) {
                i--;
            }
            i++;

            Node child = read_node(node.children[i]);
            if (child.num_keys == ORDER) {
                split_child(node_pos, i);
                node = read_node(node_pos);
                if (node.pairs[i] < pair) {
                    i++;
                }
            }
            insert_non_full(node.children[i], pair);
        }
    }

    void merge_or_redistribute(int parent_pos, int index) {
        Node parent = read_node(parent_pos);
        Node child = read_node(parent.children[index]);

        int min_keys = (ORDER + 1) / 2;

        if (index > 0) {
            Node left_sibling = read_node(parent.children[index - 1]);
            if (left_sibling.num_keys > min_keys) {
                // Borrow from left sibling
                for (int i = child.num_keys; i > 0; i--) {
                    child.pairs[i] = child.pairs[i - 1];
                }
                if (!child.is_leaf) {
                    for (int i = child.num_keys + 1; i > 0; i--) {
                        child.children[i] = child.children[i - 1];
                    }
                }

                child.pairs[0] = left_sibling.pairs[left_sibling.num_keys - 1];
                if (!child.is_leaf) {
                    child.children[0] = left_sibling.children[left_sibling.num_keys];
                }

                left_sibling.num_keys--;
                child.num_keys++;

                parent.pairs[index - 1] = child.pairs[0];

                write_node(parent_pos, parent);
                write_node(parent.children[index - 1], left_sibling);
                write_node(parent.children[index], child);
                return;
            }
        }

        if (index < parent.num_keys) {
            Node right_sibling = read_node(parent.children[index + 1]);
            if (right_sibling.num_keys > min_keys) {
                // Borrow from right sibling
                child.pairs[child.num_keys] = right_sibling.pairs[0];
                if (!child.is_leaf) {
                    child.children[child.num_keys + 1] = right_sibling.children[0];
                }
                child.num_keys++;

                for (int i = 0; i < right_sibling.num_keys - 1; i++) {
                    right_sibling.pairs[i] = right_sibling.pairs[i + 1];
                }
                if (!right_sibling.is_leaf) {
                    for (int i = 0; i < right_sibling.num_keys; i++) {
                        right_sibling.children[i] = right_sibling.children[i + 1];
                    }
                }
                right_sibling.num_keys--;

                parent.pairs[index] = right_sibling.pairs[0];

                write_node(parent_pos, parent);
                write_node(parent.children[index], child);
                write_node(parent.children[index + 1], right_sibling);
                return;
            }
        }

        // Merge with sibling
        if (index > 0) {
            // Merge with left sibling
            Node left_sibling = read_node(parent.children[index - 1]);

            for (int i = 0; i < child.num_keys; i++) {
                left_sibling.pairs[left_sibling.num_keys + i] = child.pairs[i];
            }
            if (!child.is_leaf) {
                for (int i = 0; i <= child.num_keys; i++) {
                    left_sibling.children[left_sibling.num_keys + i] = child.children[i];
                }
            } else {
                left_sibling.next_leaf = child.next_leaf;
            }
            left_sibling.num_keys += child.num_keys;

            write_node(parent.children[index - 1], left_sibling);

            for (int i = index - 1; i < parent.num_keys - 1; i++) {
                parent.pairs[i] = parent.pairs[i + 1];
                parent.children[i + 1] = parent.children[i + 2];
            }
            parent.num_keys--;

            write_node(parent_pos, parent);
        } else {
            // Merge with right sibling
            Node right_sibling = read_node(parent.children[index + 1]);

            for (int i = 0; i < right_sibling.num_keys; i++) {
                child.pairs[child.num_keys + i] = right_sibling.pairs[i];
            }
            if (!child.is_leaf) {
                for (int i = 0; i <= right_sibling.num_keys; i++) {
                    child.children[child.num_keys + i] = right_sibling.children[i];
                }
            } else {
                child.next_leaf = right_sibling.next_leaf;
            }
            child.num_keys += right_sibling.num_keys;

            write_node(parent.children[index], child);

            for (int i = index; i < parent.num_keys - 1; i++) {
                parent.pairs[i] = parent.pairs[i + 1];
                parent.children[i + 1] = parent.children[i + 2];
            }
            parent.num_keys--;

            write_node(parent_pos, parent);
        }
    }

    bool delete_from_node(int node_pos, const DataPair& pair) {
        Node node = read_node(node_pos);

        if (node.is_leaf) {
            int i = 0;
            while (i < node.num_keys && node.pairs[i] < pair) {
                i++;
            }

            if (i < node.num_keys && node.pairs[i] == pair) {
                for (int j = i; j < node.num_keys - 1; j++) {
                    node.pairs[j] = node.pairs[j + 1];
                }
                node.num_keys--;
                write_node(node_pos, node);
                return true;
            }
            return false;
        } else {
            int i = 0;
            while (i < node.num_keys && pair.key >= node.pairs[i].key) {
                i++;
            }

            bool deleted = delete_from_node(node.children[i], pair);

            if (deleted) {
                Node child = read_node(node.children[i]);
                int min_keys = (ORDER + 1) / 2;

                if (child.num_keys < min_keys && node_pos != root) {
                    merge_or_redistribute(node_pos, i);
                }
            }

            return deleted;
        }
    }

public:
    BPlusTree(const string& fname) : filename(fname), root(-1), node_count(0) {
        ifstream test(filename);
        if (test.good()) {
            test.close();
            file.open(filename, ios::in | ios::out | ios::binary);
            read_metadata();
        } else {
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);

            root = allocate_node();
            Node root_node;
            write_node(root, root_node);
            write_metadata();
        }
    }

    ~BPlusTree() {
        if (file.is_open()) {
            write_metadata();
            file.close();
        }
    }

    void insert(const Key& key, int value) {
        DataPair pair(key, value);

        if (root == -1) {
            root = allocate_node();
            Node root_node;
            write_node(root, root_node);
        }

        Node root_node = read_node(root);

        if (root_node.num_keys == ORDER) {
            int new_root = allocate_node();
            Node new_root_node;
            new_root_node.is_leaf = false;
            new_root_node.num_keys = 0;
            new_root_node.children[0] = root;
            write_node(new_root, new_root_node);

            split_child(new_root, 0);
            root = new_root;
            write_metadata();
            insert_non_full(root, pair);
        } else {
            insert_non_full(root, pair);
        }
    }

    void remove(const Key& key, int value) {
        if (root == -1) return;

        DataPair pair(key, value);
        delete_from_node(root, pair);

        Node root_node = read_node(root);
        if (root_node.num_keys == 0 && !root_node.is_leaf) {
            root = root_node.children[0];
            write_metadata();
        }
    }

    vector<int> find(const Key& key) {
        vector<int> result;
        if (root == -1) return result;

        int current = root;
        while (true) {
            Node node = read_node(current);

            if (node.is_leaf) {
                for (int i = 0; i < node.num_keys; i++) {
                    if (node.pairs[i].key == key) {
                        result.push_back(node.pairs[i].value);
                    }
                }

                // Check next leaf nodes
                while (node.next_leaf != -1) {
                    node = read_node(node.next_leaf);
                    if (node.num_keys > 0 && node.pairs[0].key == key) {
                        for (int i = 0; i < node.num_keys && node.pairs[i].key == key; i++) {
                            result.push_back(node.pairs[i].value);
                        }
                    } else {
                        break;
                    }
                }
                break;
            } else {
                int i = 0;
                while (i < node.num_keys && key >= node.pairs[i].key) {
                    i++;
                }
                current = node.children[i];
            }
        }

        sort(result.begin(), result.end());
        return result;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    BPlusTree tree("data.dat");

    int n;
    cin >> n;

    string command;
    for (int i = 0; i < n; i++) {
        cin >> command;

        if (command == "insert") {
            char index[MAX_KEY_SIZE];
            int value;
            cin >> index >> value;
            tree.insert(Key(index), value);
        } else if (command == "delete") {
            char index[MAX_KEY_SIZE];
            int value;
            cin >> index >> value;
            tree.remove(Key(index), value);
        } else if (command == "find") {
            char index[MAX_KEY_SIZE];
            cin >> index;
            vector<int> result = tree.find(Key(index));

            if (result.empty()) {
                cout << "null\n";
            } else {
                for (size_t j = 0; j < result.size(); j++) {
                    if (j > 0) cout << " ";
                    cout << result[j];
                }
                cout << "\n";
            }
        }
    }

    return 0;
}
