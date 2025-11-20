#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstdlib>

#ifdef _WIN32
    #include <direct.h>  // For _getcwd on Windows
#else
    #include <unistd.h>  // For getcwd on Linux/macOS
#endif

// Simple 2D particle representation
struct Particle {
    double x;
    double y;
};

// Helper: squared distance between two particles
double dist2(const Particle& a, const Particle& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Function to open VTK file with ParaView
void openVTKFile(const std::string& filename) {
    // Get absolute path
    char buffer[256];
    #ifdef _WIN32
        // Get current working directory on Windows
        if (_getcwd(buffer, sizeof(buffer)) != nullptr) {
            std::string fullPath = std::string(buffer) + "\\" + filename;
            
            // Try to find ParaView installation
            const char* paraviewPaths[] = {
                "C:\\Program Files\\ParaView 6.0.1\\bin\\paraview.exe",
                "C:\\Program Files\\ParaView\\bin\\paraview.exe",
                "C:\\Program Files (x86)\\ParaView 6.0.1\\bin\\paraview.exe",
                "C:\\Program Files (x86)\\ParaView\\bin\\paraview.exe"
            };
            
            std::string command;
            bool found = false;
            
            // Check if any ParaView installation exists
            for (const char* path : paraviewPaths) {
                std::ifstream f(path);
                if (f.good()) {
                    // Use cmd /c to properly execute the command with spaces in paths
                    command = "cmd /c \"\"" + std::string(path) + "\" \"" + fullPath + "\"\"";
                    found = true;
                    break;
                }
            }
            
            // If ParaView not found in standard locations, try using 'where' command
            if (!found) {
                std::string whereCmd = "where paraview.exe > temp_paraview_path.txt 2>nul";
                if (std::system(whereCmd.c_str()) == 0) {
                    std::ifstream pathFile("temp_paraview_path.txt");
                    std::string paraviewPath;
                    if (std::getline(pathFile, paraviewPath)) {
                        // Remove trailing whitespace
                        paraviewPath.erase(paraviewPath.find_last_not_of(" \n\r\t") + 1);
                        command = "cmd /c \"\"" + paraviewPath + "\" \"" + fullPath + "\"\"";
                        found = true;
                    }
                    pathFile.close();
                    std::remove("temp_paraview_path.txt");
                }
            }
            
            if (found) {
                int result = std::system(command.c_str());
                if (result == 0 || result == 1) {  // ParaView may return various exit codes
                    std::cout << "Opening " << filename << " with ParaView...\n";
                    return;
                }
            }
        }
    #endif
    
    std::cout << "\nNote: Could not automatically open " << filename 
              << " with ParaView.\n";
    std::cout << "File location: " << std::string(buffer) << "\\" << filename << "\n";
    std::cout << "\nPlease open it manually:\n";
    std::cout << "1. Open ParaView (C:\\Program Files\\ParaView 6.0.1\\bin\\paraview.exe)\n";
    std::cout << "2. Go to File > Open\n";
    std::cout << "3. Navigate to the file above and select it\n";
}

// Main simulation function
void runSimulation() {
    // -----------------------------
    // 1. Read user input
    // -----------------------------
    double Lx, Ly;       // Domain size in x and y
    double dx;           // Discretization size
    double phi;          // Target porosity ratio (0..1)
    double m;            // Horizon factor (delta = m * dx)

    std::cout << "\n===== Peridynamic Porosity Simulation =====\n";
    std::cout << "Enter domain length in x (Lx): ";
    std::cin >> Lx;
    std::cout << "Enter domain length in y (Ly): ";
    std::cin >> Ly;
    std::cout << "Enter discretization size (dx): ";
    std::cin >> dx;
    std::cout << "Enter porosity ratio phi (0.1): ";
    std::cin >> phi;
    std::cout << "Enter horizon factor m (delta = m*dx): ";
    std::cin >> m;

    if (dx <= 0.0 || Lx <= 0.0 || Ly <= 0.0 || phi < 0.0 || phi > 1.0 || m <= 0.0) {
        std::cerr << "Invalid input parameters.\n";
        return;
    }

    // Pre-damage index d_phi = phi / phi_c, with phi_c = 1.0
    double d_phi = phi;  // since phi_c = 1.0

    // -----------------------------
    // 2. Build regular grid of particles
    // -----------------------------
    int Nx = static_cast<int>(std::floor(Lx / dx)) + 1;
    int Ny = static_cast<int>(std::floor(Ly / dx)) + 1;
    int N = Nx * Ny;

    std::cout << "Building grid: Nx = " << Nx
        << ", Ny = " << Ny
        << ", total particles N = " << N << "\n";

    std::vector<Particle> particles(N);

    // Fill grid (row-major: j = y-direction, i = x-direction)
    for (int j = 0; j < Ny; ++j) {
        for (int i = 0; i < Nx; ++i) {
            int id = j * Nx + i;
            particles[id].x = i * dx;
            particles[id].y = j * dx;
        }
    }

    // -----------------------------
    // 3. Compute neighbor counts N(i)
    // -----------------------------
    double delta = m * dx;
    double delta2 = delta * delta;

    std::vector<int> N_total(N, 0);   // N(i): total number of bonds for each particle
    std::vector<int> N_broken(N, 0);  // Nb(i): number of broken bonds for each particle

    // First pass: determine total number of bonds per particle
    // (no damage applied yet)
    std::cout << "Computing neighbors (N(i))...\n";
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            if (dist2(particles[i], particles[j]) <= delta2) {
                // i and j are neighbors -> one bond
                N_total[i]++;
                N_total[j]++;
            }
        }
    }

    // -----------------------------
    // 4. Apply pre-damage algorithm (uniform porosity)
    // -----------------------------
    std::cout << "Applying pre-damage (uniform porosity)...\n";

    // Random number in [0,1)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> uniform01(0.0, 1.0);

    int totalBonds = 0;
    int brokenBonds = 0;

    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            if (dist2(particles[i], particles[j]) <= delta2) {
                totalBonds++;
                double r = uniform01(gen);

                // Uniform porosity: d_phi(i) is same for all i
                if (r < d_phi) {
                    // break bond (i,j)
                    N_broken[i]++;
                    N_broken[j]++;
                    brokenBonds++;
                }
            }
        }
    }

    double realizedPorosity = 0.0;
    if (totalBonds > 0) {
        realizedPorosity = static_cast<double>(brokenBonds) / static_cast<double>(totalBonds);
    }

    std::cout << "Total bonds (before damage): " << totalBonds << "\n";
    std::cout << "Broken bonds (after damage): " << brokenBonds << "\n";
    std::cout << "Realized global porosity (bond-based) ~ " << realizedPorosity << "\n";

    // -----------------------------
    // 5. Compute local damage d(i) = Nb(i) / N(i)
    // -----------------------------
    std::vector<double> damage(N, 0.0);
    for (int i = 0; i < N; ++i) {
        if (N_total[i] > 0) {
            damage[i] = static_cast<double>(N_broken[i]) /
                static_cast<double>(N_total[i]);
        }
        else {
            damage[i] = 0.0;  // isolated point, no neighbors
        }
    }

    // -----------------------------
    // 6. Write VTK file for visualization
    // -----------------------------
    // Generate unique filename based on parameters
    std::string filename = "porosity_Lx" + std::to_string(static_cast<int>(Lx)) 
                         + "_phi" + std::to_string(static_cast<int>(phi * 100)) + ".vtk";
    std::ofstream vtk(filename);
    if (!vtk) {
        std::cerr << "Error: could not open " << filename << " for writing.\n";
        return;
    }

    vtk << "# vtk DataFile Version 3.0\n";
    vtk << "Peridynamic porous pre-damage\n";
    vtk << "ASCII\n";
    vtk << "DATASET POLYDATA\n";

    // Write points
    vtk << "POINTS " << N << " float\n";
    vtk << std::fixed << std::setprecision(6);
    for (int i = 0; i < N; ++i) {
        vtk << particles[i].x << " "
            << particles[i].y << " "
            << 0.0 << "\n";  // z = 0 for 2D surface
    }

    // Each point as a separate vertex cell
    vtk << "VERTICES " << N << " " << 2 * N << "\n";
    for (int i = 0; i < N; ++i) {
        vtk << "1 " << i << "\n";
    }

    // Point data
    vtk << "POINT_DATA " << N << "\n";
    vtk << "SCALARS damage float 1\n";
    vtk << "LOOKUP_TABLE default\n";
    for (int i = 0; i < N; ++i) {
        vtk << damage[i] << "\n";
    }

    vtk.close();
    std::cout << "\nVTK file written to: " << filename << "\n";

    // Offer to open the VTK file
    std::cout << "\nWould you like to visualize the results? (y/n): ";
    char visualize;
    std::cin >> visualize;
    
    if (visualize == 'y' || visualize == 'Y') {
        openVTKFile(filename);
        std::cout << "If the file didn't open automatically, you can open it manually with ParaView.\n";
        std::cout << "ParaView (free): https://www.paraview.org/download/\n";
    }
}

int main() {
    bool continueSimulations = true;

    while (continueSimulations) {
        runSimulation();

        // Ask user if they want to run another simulation
        std::cout << "\n========================================\n";
        std::cout << "Would you like to run another simulation? (y/n): ";
        char response;
        std::cin >> response;

        if (response != 'y' && response != 'Y') {
            continueSimulations = false;
        }
    }

    std::cout << "\nThank you for using the Peridynamic Porosity Simulator!\n";
    std::cout << "Press Enter to exit...\n";
    std::cin.ignore();
    std::cin.get();

    return 0;
}
