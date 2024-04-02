# Notes

## Base consumer thread
- Create ```EGLStream::FrameConsumer``` object --> Read frames from ```OutputStream``` --> create/populate ```NvBuffer (dmabuf)``` from frames --> Then processed by ```processV4L2Fd```

## Importance of ```protected member functions```

Protected member functions in a C++ class have a crucial role in facilitating the implementation of ```inheritance``` and ```encapsulation```. Here's why they're important:

1. **Access within derived classes**: Protected member functions can be accessed by derived classes. This allows derived classes to reuse and extend functionality provided by the base class without exposing it to the outside world. 

2. **Implementation hiding**: They help in hiding the implementation details of the base class from users of derived classes. This promotes information hiding and reduces dependencies between different parts of the program.

3. **Polymorphism**: Protected member functions can participate in polymorphism. Derived classes can override these functions to provide specialized implementations while still preserving the interface defined by the base class.

### Practical Example

```cpp
#include <iostream>

class Shape {
public:
    Shape() {}
    virtual ~Shape() {}

    // Public interface
    void draw() {
        drawShape(); // Calls protected member function
    }

protected:
    // Protected member function
    virtual void drawShape() {
        std::cout << "Drawing a generic shape." << std::endl;
    }
};

class Circle : public Shape {
protected:
    // Override protected member function
    void drawShape() override {
        std::cout << "Drawing a circle." << std::endl;
    }
};

class Square : public Shape {
protected:
    // Override protected member function
    void drawShape() override {
        std::cout << "Drawing a square." << std::endl;
    }
};

int main() {
    Shape* circle = new Circle();
    Shape* square = new Square();

    circle->draw(); // Output: Drawing a circle.
    square->draw(); // Output: Drawing a square.

    delete circle;
    delete square;

    return 0;
}
